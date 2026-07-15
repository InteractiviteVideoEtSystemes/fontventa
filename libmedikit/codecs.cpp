/**
 * codecs.cpp — catalogue des capacités codec (phase 1 nego_fmtp).
 *
 * Ne contient AUCUNE logique de disponibilité propre à une lib : chaque classe
 * de codec porte sa propre méthode statique IsSupported() (native -> true,
 * ffmpeg -> délègue à FfAudio/VideoDecoder::IsCodecAvailable avec SON id/nom,
 * autre lib -> son propre test). Ce fichier ne fait que :
 *   - DISPATCHER AudioCodec/VideoCodec::IsSupported(Type) vers la classe du codec
 *     (même mapping Type->classe que les factories CreateDecoder) ;
 *   - exposer GetSupportedCodecs() (liste de candidats filtrée, mémoïsée).
 * Ainsi l'intégration d'un codec sur une autre lib ne touche que sa classe.
 */
#include "medkit/codecs.h"
#include "medkit/audio.h"
#include "medkit/video.h"

// Classes de codec concrètes : chacune fournit son IsSupported().
#include "g711/g711codec.h"
#include "opus/opuscodec.h"
#include "aac/aacdecoder.h"
#include "amr/amrcodec.h"
#include "gsm/gsmcodec.h"
#include "speex/speexcodec.h"
#include "g722/g722codec.h"
#include "nelly/nellycodec.h"
#include "h264/h264decoder.h"
#include "vp8/vp8decoder.h"
#include "av1/av1codec.h"
#include "h263/h263codec.h"
#include "ffvideocodec.h"	// FfVideoDecoder::IsCodecAvailable pour les types legacy

#include <mutex>
#include <string>

/* ----------------------------- Audio ----------------------------- */

bool AudioCodec::IsSupported(AudioCodec::Type codec)
{
	// « Supporté » = on sait DÉCODER ce codec (sens réception, décision D).
	switch (codec)
	{
		case PCMA:            return PCMADecoder::IsSupported();
		case PCMU:            return PCMUDecoder::IsSupported();
		case GSM:             return GSMDecoder::IsSupported();
		case G722:            return G722Decoder::IsSupported();
		case SPEEX16:         return SpeexDecoder::IsSupported();
		case AMR:             return AMRNBDecoder::IsSupported();
		case AMRWB:           return AMRWBDecoder::IsSupported();
		case AAC:             return AACDecoder::IsSupported();
		case OPUS:            return OPUSDecoder::IsSupported();
		case NELLY8:          return NellyDecoder::IsSupported();
		case NELLY11:         return NellyDecoder11Khz::IsSupported();
		// Pas de classe décodeur dédiée (événements DTMF / PCM brut).
		case TELEPHONE_EVENT:
		case SLIN:            return true;
		// Type inconnu : ne pas prétendre le supporter (autorité du serveur,
		// décision D). Écart assumé vs la mention « défaut true » du plan : un
		// catalogue de capacités doit être conservateur.
		default:              return false;
	}
}

const std::vector<AudioCodec::Type>& AudioCodecFactory::GetSupportedCodecs()
{
	// Candidats = types réellement instanciables par CreateEncoder/CreateDecoder,
	// dans l'ordre de préférence. Filtrés une fois par IsSupported().
	static const AudioCodec::Type candidates[] = {
		AudioCodec::OPUS,
		AudioCodec::PCMU,
		AudioCodec::PCMA,
		AudioCodec::G722,
		AudioCodec::AAC,
		AudioCodec::AMRWB,
		AudioCodec::AMR,
		AudioCodec::SPEEX16,
		AudioCodec::GSM,
		AudioCodec::NELLY11,
		AudioCodec::NELLY8,
	};
	static std::vector<AudioCodec::Type> cache;
	static std::once_flag once;
	std::call_once(once, [] {
		for (AudioCodec::Type t : candidates)
			if (AudioCodec::IsSupported(t))
				cache.push_back(t);
	});
	return cache;
}

/* ----------------------------- Video ----------------------------- */

bool VideoCodec::IsSupported(VideoCodec::Type codec)
{
	switch (codec)
	{
		case H264:      return H264Decoder::IsSupported();
		case VP8:       return VP8Decoder::IsSupported();
		case AV1:       return AV1Decoder::IsSupported();
		case H263_1998: return H263Decoder::IsSupported();
		// Types « legacy » sans classe dédiée : la factory les instancie via un
		// FfVideoDecoder générique ; on interroge la même primitive ffmpeg (le
		// mapping Type->AVCodecID est celui de VideoCodecFactory::CreateDecoder).
		case H263_1996: return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_H263);
		case MPEG4:     return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_MPEG4);
		case SORENSON:  return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_FLV1);
		case VP6:       return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_VP6F);
		// ULPFEC/RED ne sont pas des codecs média ; type inconnu -> non supporté.
		default:        return false;
	}
}

const std::vector<VideoCodec::Type>& VideoCodecFactory::GetSupportedCodecs()
{
	static const VideoCodec::Type candidates[] = {
		VideoCodec::H264,
		VideoCodec::VP8,
		VideoCodec::AV1,
		VideoCodec::H263_1998,
		VideoCodec::H263_1996,
		VideoCodec::MPEG4,
		VideoCodec::SORENSON,
		VideoCodec::VP6,
	};
	static std::vector<VideoCodec::Type> cache;
	static std::once_flag once;
	std::call_once(once, [] {
		for (VideoCodec::Type t : candidates)
			if (VideoCodec::IsSupported(t))
				cache.push_back(t);
	});
	return cache;
}

/* ----------------------------- Text ------------------------------ */

bool TextCodec::IsSupported(TextCodec::Type codec)
{
	// T140/T140RED sont gérés nativement (pas de dépendance externe).
	switch (codec)
	{
		case T140:
		case T140RED:
			return true;
		default:
			return false;
	}
}

std::string TextCodec::GetT140RedFmtpParams(int t140PayloadType, int generations)
{
	// RFC 4103 : le fmtp du RED texte liste les PT des générations redondantes +
	// primaire, séparés par '/'. Ici toutes les générations portent le même PT
	// T140 (un seul codec texte). Au moins une génération.
	if (generations < 1)
		generations = 1;
	std::string pt = std::to_string(t140PayloadType);
	std::string out = pt;
	for (int i = 1; i < generations; i++)
		out += "/" + pt;
	return out;
}

const std::vector<TextCodec::Type>& TextCodecFactory::GetSupportedCodecs()
{
	static const TextCodec::Type candidates[] = {
		TextCodec::T140RED,
		TextCodec::T140,
	};
	static std::vector<TextCodec::Type> cache;
	static std::once_flag once;
	std::call_once(once, [] {
		for (TextCodec::Type t : candidates)
			if (TextCodec::IsSupported(t))
				cache.push_back(t);
	});
	return cache;
}

/* ------------------------------ App ------------------------------ */

bool AppCodec::IsSupported(AppCodec::Type codec)
{
	// BFCP est un protocole applicatif (pas un codec) : toujours disponible.
	switch (codec)
	{
		case BFCP:
			return true;
		default:
			return false;
	}
}
