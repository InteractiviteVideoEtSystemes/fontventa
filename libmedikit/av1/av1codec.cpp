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
// Lecteur de bits MSB-first minimal, utilisé pour parcourir les OBU du flux
// encodé (retrait du temporal delimiter, capture du sequence header pour
// GetFmtpInfo). La lecture leb128 (AV1 spec §4.10.5) vit dans
// av1depacketizer.h : un seul lecteur pour les deux sens.
//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////
// AV1Encoder
//////////////////////////////////////////////////////////////////////////

AV1Encoder::AV1Encoder(const Properties& properties) :
	// "libsvtav1" : encodeur temps-réel capable (presets 0-13). ffmpeg
	// choisirait "libaom-av1" par défaut, bien trop lent pour un flux live.
	FfVideoEncoder(properties, AV_CODEC_ID_AV1, VideoCodec::AV1, /*tryHW*/ false, "libsvtav1"),
	openedBitrate(0), obuSeqHdrCached(false), cachedProfile(0), cachedLevelIdx(0), cachedTier(0)
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

	openedBitrate = bitrate;
}

int AV1Encoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Mémorise fps/débit/période intra
	FfVideoEncoder::SetFrameRate(frames,kbits,intraPeriod);

	// Réouverture seulement sur une variation significative (>=10 %) : chaque
	// réouverture coûte une trame clé, on ne la paye pas à chaque
	// micro-ajustement de la boucle d'adaptation.
	if (opened && openedBitrate > 0 && abs(bitrate-openedBitrate)*10 >= openedBitrate)
		ReopenCodec();

	return 1;
}

// Paquetisation RTP AV1 (spec « RTP Payload Format For AV1 »), plus la capture du
// sequence header pour GetFmtpInfo — les deux se font sur le même parcours des
// OBU du flux encodé (format low-overhead : chaque OBU porte son obu_size).
//
// La trame n'est PAS réécrite. Le découpage se décrit en offsets dans le tampon
// que ffmpeg vient de remplir (AddRtpPacket : un préfixe court + une tranche),
// donc ni malloc ni recopie par image. C'est aussi plus juste pour les autres
// consommateurs de la trame : la version précédente retirait le temporal
// delimiter du tampon lui-même, alors qu'un fichier AV1 en veut un en tête de
// chaque unité temporelle — seul le FIL n'en veut pas, et c'est le paquetiseur
// qui s'en charge maintenant.
void AV1Encoder::PacketizeFrame()
{
	BYTE* data = frame->GetData();
	DWORD len  = frame->GetLength();

	std::vector<AV1ObuRef> obus;

	if (!AV1ParseObuStream(data, len, obus))
	{
		Log("-AV1Encoder: flux OBU incoherent, packetisation par defaut\n");
		FfVideoEncoder::PacketizeFrame();
		return;
	}

	if (!obuSeqHdrCached)
	{
		for (size_t i = 0; i < obus.size(); i++)
		{
			if (obus[i].type != AV1_OBU_SEQUENCE_HEADER)
				continue;

			int profile, levelIdx, tier;
			if (ParseAV1SeqHdr(data + obus[i].payloadPos, obus[i].payloadLen,
			                   profile, levelIdx, tier))
			{
				cachedProfile   = profile;
				cachedLevelIdx  = levelIdx;
				cachedTier      = tier;
				obuSeqHdrCached = true;
			}
			break;
		}
	}

	std::vector<AV1RtpPacket> packets;

	if (!AV1PacketizeTemporalUnit(data, len, RTPPAYLOADSIZE, packets))
	{
		FfVideoEncoder::PacketizeFrame();
		return;
	}

	for (size_t i = 0; i < packets.size(); i++)
		frame->AddRtpPacket(packets[i].pos, packets[i].size,
		                    packets[i].prefix, packets[i].prefixLen,
		                    packets[i].mark);
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

/***********************
* AV1Decoder::DecodePacket
*	Dépaquetise le flux RTP AV1 puis décode l'unité temporelle sur le bit
*	marqueur. Tout l'état (fragments d'OBU, sequence header en cache) vit dans
*	AV1Depacketizer ; `buffer`/`bufLen` héritées ne servent plus qu'à donner à
*	ffmpeg une zone avec son padding.
*
*	Sans cette redéfinition, l'accumulation brute de FfVideoDecoder::DecodePacket
*	donnait à libdav1d l'octet d'agrégation RTP en tête de charge : lu comme un
*	obu_header, il annonçait des types d'OBU qui n'existent pas (« Unknown OBU
*	type 11 »), et pas une seule image n'était jamais décodée.
************************/
int AV1Decoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	depacketizer.AddPayload(in, inLen, lost != 0);

	// Unité temporelle incomplète : on attend le bit marqueur.
	if (!last)
		return 1;

	DWORD       len = 0;
	const BYTE* tu  = depacketizer.GetTemporalUnit(len);

	// Rien de décodable (unité amputée, ou sequence header jamais vu) : rendre 0
	// est le signal utile — l'appelant demande une image clé, bornée à une par
	// seconde, et c'est elle qui redonne le sequence header.
	if (!tu || !len)
		return 0;

	if (len + AV_INPUT_BUFFER_PADDING_SIZE > bufSize)
	{
		Log("-AV1 DecodePacket: temporal unit too large [%u > %u], dropping\n", len, bufSize);
		return 0;
	}

	// La recopie n'est pas gratuite mais elle est obligatoire : ffmpeg lit
	// au-delà de la fin des données et exige AV_INPUT_BUFFER_PADDING_SIZE octets
	// nuls, que le vecteur du dépaquetiseur ne garantit pas.
	memcpy(buffer, tu, len);
	memset(buffer + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

	return Decode(buffer, len);
}
