/*
 * File:   av1codec.cpp
 *
 * Encodeur AV1 (libsvtav1, temps-réel) / décodeur AV1 (libdav1d).
 */
#include <string.h>
#include <sstream>
#include "medkit/log.h"
#include "av1codec.h"

extern "C" {
#include <libavutil/opt.h>
}

//////////////////////////////////////////////////////////////////////////
// Lecture leb128 (AV1 spec §4.10.5) et lecteur de bits MSB-first minimal,
// utilisés pour parcourir les OBU du flux encodé (retrait du temporal
// delimiter, capture du sequence header pour GetFmtpInfo).
//////////////////////////////////////////////////////////////////////////

static bool ReadLeb128(const uint8_t* data, size_t size, size_t& consumed, uint64_t& value)
{
	value = 0;
	consumed = 0;
	for (int i = 0; i < 8; i++)
	{
		if (consumed >= size)
			return false;
		uint8_t b = data[consumed];
		value |= (uint64_t)(b & 0x7f) << (i * 7);
		consumed++;
		if (!(b & 0x80))
			return true;
	}
	return false; // leb128 trop long (non conforme)
}

namespace {

class BitReader
{
public:
	BitReader(const uint8_t* data, size_t size) : data(data), size(size), pos(0) {}

	bool ReadBits(int n, uint32_t& out)
	{
		out = 0;
		for (int i = 0; i < n; i++)
		{
			size_t bytePos = pos / 8;
			if (bytePos >= size)
				return false;
			int bitPos = 7 - (int)(pos % 8);
			out = (out << 1) | ((data[bytePos] >> bitPos) & 1);
			pos++;
		}
		return true;
	}

private:
	const uint8_t* data;
	size_t size;
	size_t pos;
};

} // namespace

// Extrait profile/seq_level_idx[0]/seq_tier[0] d'un sequence_header_obu
// (AV1 spec §5.5.1), pour le seul point de fonctionnement 0 : suffisant pour
// un encodeur temps réel (SVT-AV1) qui n'émet qu'un point de fonctionnement.
// N'implémente pas timing_info()/decoder_model_info() (uvlc/champs 32 bits) :
// si timing_info_present_flag est à 1, on abandonne plutôt que de mal parser.
static bool ParseAV1SeqHdr(const uint8_t* data, size_t size, int& profile, int& levelIdx, int& tier)
{
	BitReader br(data, size);
	uint32_t v;

	if (!br.ReadBits(3, v)) return false;
	profile = (int)v;

	if (!br.ReadBits(1, v)) return false; // still_picture

	uint32_t reducedStillPictureHeader;
	if (!br.ReadBits(1, reducedStillPictureHeader)) return false;

	if (reducedStillPictureHeader)
	{
		if (!br.ReadBits(5, v)) return false;
		levelIdx = (int)v;
		tier = 0;
		return true;
	}

	uint32_t timingInfoPresentFlag;
	if (!br.ReadBits(1, timingInfoPresentFlag)) return false;
	if (timingInfoPresentFlag)
		return false; // timing_info()/decoder_model_info() non géré

	uint32_t initialDisplayDelayPresentFlag;
	if (!br.ReadBits(1, initialDisplayDelayPresentFlag)) return false;

	uint32_t operatingPointsCntMinus1;
	if (!br.ReadBits(5, operatingPointsCntMinus1)) return false;

	// Seul le point de fonctionnement 0 nous intéresse.
	uint32_t operatingPointIdc0;
	if (!br.ReadBits(12, operatingPointIdc0)) return false;

	uint32_t seqLevelIdx0;
	if (!br.ReadBits(5, seqLevelIdx0)) return false;
	levelIdx = (int)seqLevelIdx0;

	if (seqLevelIdx0 > 7)
	{
		uint32_t seqTier0;
		if (!br.ReadBits(1, seqTier0)) return false;
		tier = (int)seqTier0;
	}
	else
	{
		tier = 0;
	}

	return true;
}

enum
{
	OBU_SEQUENCE_HEADER     = 1,
	OBU_TEMPORAL_DELIMITER  = 2,
};

//////////////////////////////////////////////////////////////////////////
// AV1Encoder
//////////////////////////////////////////////////////////////////////////

AV1Encoder::AV1Encoder(const Properties& properties) :
	// "libsvtav1" : encodeur temps-réel capable (presets 0-13). ffmpeg
	// choisirait "libaom-av1" par défaut, bien trop lent pour un flux live.
	FfVideoEncoder(properties, AV_CODEC_ID_AV1, VideoCodec::AV1, /*tryHW*/ false, "libsvtav1"),
	obuSeqHdrCached(false), cachedProfile(0), cachedLevelIdx(0), cachedTier(0)
{
	preset = properties.GetProperty("av1.preset", 10);
}

AV1Encoder::~AV1Encoder()
{
}

void AV1Encoder::ConfigureContext()
{
	// Un (ré)ouverture (resize...) peut changer le sequence header : invalide
	// le cache utilisé par GetFmtpInfo (même logique que H264Encoder).
	obuSeqHdrCached = false;

	if (ctx && ctx->priv_data)
		av_opt_set_int(ctx->priv_data, "preset", preset, 0);
}

// Parcourt les OBU du flux encodé (format bas-overhead : chaque OBU porte son
// obu_size en leb128) : retire l'OBU de temporal delimiter (RFC AV1 RTP : ne
// doit jamais être transmis), capture le premier sequence header pour
// GetFmtpInfo. La packetisation RTP proprement dite (agrégation d'OBU par
// paquet, cf. plan) n'est pas encore implémentée : on retombe sur le
// découpage générique de FfVideoEncoder, non conforme au format RTP AV1 mais
// suffisant pour valider build/factory/GetFmtpInfo en attendant.
void AV1Encoder::PacketizeFrame()
{
	BYTE* data  = frame->GetData();
	DWORD len   = frame->GetLength();

	BYTE* out = (BYTE*)malloc(len ? len : 1);
	DWORD outLen = 0;

	size_t pos = 0;
	bool malformed = false;

	while (pos < len && !malformed)
	{
		size_t obuStart = pos;
		uint8_t hdr = data[pos];
		uint8_t obuType   = (hdr >> 3) & 0x0f;
		bool    extFlag   = (hdr >> 2) & 0x1;
		bool    hasSize   = (hdr >> 1) & 0x1;
		size_t  headerLen = 1 + (extFlag ? 1 : 0);

		if (pos + headerLen > len)
		{
			malformed = true;
			break;
		}

		if (!hasSize)
		{
			// Format bas-overhead attendu partout : sans obu_size on ne peut
			// pas délimiter l'OBU en sécurité, on abandonne la découpe.
			Log("-AV1Encoder: OBU sans champ de taille, packetisation par défaut\n");
			malformed = true;
			break;
		}

		size_t   lebConsumed = 0;
		uint64_t obuSize     = 0;
		if (!ReadLeb128(data + pos + headerLen, len - pos - headerLen, lebConsumed, obuSize))
		{
			malformed = true;
			break;
		}

		size_t payloadStart = pos + headerLen + lebConsumed;
		size_t totalObuLen  = headerLen + lebConsumed + (size_t)obuSize;

		if (payloadStart + obuSize > len)
		{
			malformed = true;
			break;
		}

		if (obuType == OBU_SEQUENCE_HEADER && !obuSeqHdrCached)
		{
			int profile, levelIdx, tier;
			if (ParseAV1SeqHdr(data + payloadStart, (size_t)obuSize, profile, levelIdx, tier))
			{
				cachedProfile  = profile;
				cachedLevelIdx = levelIdx;
				cachedTier     = tier;
				obuSeqHdrCached = true;
			}
		}

		if (obuType != OBU_TEMPORAL_DELIMITER)
		{
			memcpy(out + outLen, data + obuStart, totalObuLen);
			outLen += totalObuLen;
		}

		pos += totalObuLen;
	}

	if (malformed || !outLen)
	{
		free(out);
		FfVideoEncoder::PacketizeFrame();
		return;
	}

	frame->SetMedia(out, outLen);
	free(out);

	// TODO : agrégation RTP AV1 dédiée (task packetisation), cf. plan.
	FfVideoEncoder::PacketizeFrame();
}

bool AV1Encoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	if (!obuSeqHdrCached)
		return false;

	std::ostringstream f;
	f << "a=fmtp:" << payloadType
	  << " profile=" << cachedProfile
	  << ";level-idx=" << cachedLevelIdx
	  << ";tier=" << cachedTier;

	fmtp = f.str();
	return true;
}

std::string AV1Encoder::GetFmtpParams(const Properties& properties)
{
	// Défauts : profile 0 (Main), level-idx 5 (≈ niveau 3.1, cf. AV1 RTP payload
	// format), tier 0 (Main). Surchargeables par la config si connus.
	std::ostringstream f;
	f << "profile="   << properties.GetProperty("av1.profile", 0)
	  << ";level-idx=" << properties.GetProperty("av1.level-idx", 5)
	  << ";tier="      << properties.GetProperty("av1.tier", 0);
	return f.str();
}

//////////////////////////////////////////////////////////////////////////
// AV1Decoder
//////////////////////////////////////////////////////////////////////////

AV1Decoder::AV1Decoder() :
	// "libdav1d" : déjà le choix par défaut de ffmpeg pour AV1 (vérifié),
	// forcé explicitement plutôt que de dépendre de cet ordre de résolution.
	FfVideoDecoder(AV_CODEC_ID_AV1, VideoCodec::AV1, "libdav1d")
{
}

AV1Decoder::~AV1Decoder()
{
}
