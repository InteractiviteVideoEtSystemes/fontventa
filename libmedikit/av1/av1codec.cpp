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
// Négociation (phase 5b nego_fmtp)
//////////////////////////////////////////////////////////////////////////

namespace {

// Limites par seq_level_idx (annexe A.3 de la spec bitstream AV1). Seuls les
// niveaux DÉFINIS figurent ici : 2.2/2.3 (idx 2-3), 3.2/3.3 (6-7) et 4.2/4.3
// (10-11) n'existent pas — un idx déclaré non défini se lit « le plus haut
// niveau défini inférieur ou égal ».
struct AV1LevelLimits
{
	int   idx;            // seq_level_idx
	DWORD maxPicSize;     // échantillons luma par image
	DWORD maxHSize;       // largeur max
	DWORD maxVSize;       // hauteur max
	QWORD maxDisplayRate; // échantillons luma par seconde
};

const AV1LevelLimits av1Levels[] = {
	{  0,   147456,  2048, 1152,    4423680ULL }, // 2.0
	{  1,   278784,  2816, 1584,    8363520ULL }, // 2.1
	{  4,   665856,  4352, 2448,   19975680ULL }, // 3.0
	{  5,  1065024,  5504, 3096,   31950720ULL }, // 3.1
	{  8,  2359296,  6144, 3456,   70778880ULL }, // 4.0
	{  9,  2359296,  6144, 3456,  141557760ULL }, // 4.1
	{ 12,  8912896,  8192, 4352,  267386880ULL }, // 5.0
	{ 13,  8912896,  8192, 4352,  534773760ULL }, // 5.1
	{ 14,  8912896,  8192, 4352, 1069547520ULL }, // 5.2
	{ 15,  8912896,  8192, 4352, 1069547520ULL }, // 5.3
	{ 16, 35651584, 16384, 8704, 1069547520ULL }, // 6.0
	{ 17, 35651584, 16384, 8704, 2139095040ULL }, // 6.1
	{ 18, 35651584, 16384, 8704, 4278190080ULL }, // 6.2
	{ 19, 35651584, 16384, 8704, 4278190080ULL }, // 6.3
};

const AV1LevelLimits* LimitsForLevelIdx(int levelIdx)
{
	const AV1LevelLimits* found = NULL;
	for (size_t i = 0; i < sizeof(av1Levels)/sizeof(av1Levels[0]); i++)
	{
		if (av1Levels[i].idx > levelIdx)
			break;
		found = &av1Levels[i];
	}
	// un idx sous 2.0 se lit comme 2.0 : borner plus bas n'existe pas
	return found ? found : &av1Levels[0];
}

int RemoteIntParam(const std::map<std::string,std::string>& params,
                   const char* name, int defaultValue)
{
	std::map<std::string,std::string>::const_iterator it = params.find(name);
	if (it == params.end())
		return defaultValue;
	return atoi(it->second.c_str());
}

} // namespace

void AV1Encoder::ResolveNegotiation(const Properties& localProps,
                                    const std::map<std::string,std::string>& remoteParams,
                                    Properties& announceProps,
                                    Properties& effectiveProps)
{
	// Tout ce que la négociation ne touche pas (av1.preset…) survit des deux
	// côtés, et l'annonce n'est JAMAIS un reflet : chaque camp déclare sa
	// propre capacité de réception (asymétrie par défaut, sans équivalent du
	// level-asymmetry-allowed de H.264).
	announceProps  = localProps;
	effectiveProps = localProps;

	// Rien relayé par le contrôleur : aucune contrainte exploitable (cf. le
	// commentaire de déclaration — la lecture stricte de la spec passe par un
	// relais explicite des défauts).
	if (remoteParams.empty())
		return;

	// Les paramètres omis d'un fmtp PRÉSENT valent leurs défauts spec : le
	// piège ordinaire, un pair qui n'écrit que profile déclare level-idx=5
	// (3.1) — une contrainte réelle, pas une absence de contrainte.
	const int peerProfile = RemoteIntParam(remoteParams, "profile",   0);
	const int peerLevel   = RemoteIntParam(remoteParams, "level-idx", 5);
	const int peerTier    = RemoteIntParam(remoteParams, "tier",      0);

	const int ourProfile = localProps.GetProperty("av1.profile",   0);
	const int ourLevel   = localProps.GetProperty("av1.level-idx", 5);
	const int ourTier    = localProps.GetProperty("av1.tier",      0);

	// « lesser or equal to the values declared by the receiving agent » :
	// minimum composante par composante entre notre capacité et la sienne.
	effectiveProps["av1.profile"]   = std::to_string(peerProfile < ourProfile ? peerProfile : ourProfile);
	effectiveProps["av1.level-idx"] = std::to_string(peerLevel   < ourLevel   ? peerLevel   : ourLevel);
	effectiveProps["av1.tier"]      = std::to_string(peerTier    < ourTier    ? peerTier    : ourTier);

	if (peerLevel < ourLevel)
		Log("-AV1 ResolveNegotiation: emission bounded to the peer's level "
		    "[peer level-idx=%d, ours=%d]\n", peerLevel, ourLevel);
}

bool AV1Encoder::ClampToLevel(const Properties& properties,
                              int& width, int& height, int& fps)
{
	// -1 : pas de borne négociée (la clé n'est posée que par la négociation ou
	// une config explicite) — ne rien toucher.
	const int levelIdx = properties.GetProperty("av1.level-idx", -1);
	if (levelIdx < 0 || width <= 0 || height <= 0 || fps <= 0)
		return false;

	const AV1LevelLimits* lim = LimitsForLevelIdx(levelIdx);

	int w = width;
	int h = height;
	int f = fps;

	// Taille d'abord : MaxHSize/MaxVSize/MaxPicSize, ratio conservé. Le facteur
	// est le plus contraignant des trois ; arrondi pair (chroma 4:2:0).
	double scale = 1.0;

	if ((DWORD)w > lim->maxHSize && (double)lim->maxHSize / w < scale)
		scale = (double)lim->maxHSize / w;
	if ((DWORD)h > lim->maxVSize && (double)lim->maxVSize / h < scale)
		scale = (double)lim->maxVSize / h;

	const QWORD picSize = (QWORD)w * h;
	if (picSize > lim->maxPicSize)
	{
		// sqrt sans <cmath> : approximation par bissection entière suffit ici
		double s = (double)lim->maxPicSize / picSize;
		double lo = 0.0, hi = 1.0;
		for (int i = 0; i < 32; i++)
		{
			double mid = (lo + hi) / 2;
			if (mid * mid > s) hi = mid; else lo = mid;
		}
		if (lo < scale)
			scale = lo;
	}

	if (scale < 1.0)
	{
		w = ((int)(w * scale)) & ~1;
		h = ((int)(h * scale)) & ~1;
		if (w < 2) w = 2;
		if (h < 2) h = 2;
	}

	// Cadence ensuite : picSize × fps ≤ MaxDisplayRate, plancher 1 i/s.
	if ((QWORD)w * h * f > lim->maxDisplayRate)
	{
		f = (int)(lim->maxDisplayRate / ((QWORD)w * h));
		if (f < 1)
			f = 1;
	}

	if (w == width && h == height && f == fps)
		return false;

	Log("-AV1Encoder: emission clamped to declared level-idx %d: "
	    "%dx%d@%d -> %dx%d@%d\n", levelIdx, width, height, fps, w, h, f);

	width  = w;
	height = h;
	fps    = f;
	return true;
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
