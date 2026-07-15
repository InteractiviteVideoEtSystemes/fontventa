/*
 * File:   amrcodec.h
 *
 * AMR-NB / AMR-WB branchés sur la base générique FfAudioEncoder/FfAudioDecoder
 * (ffmpeg). L'encodage utilise les encodeurs externes intégrés à libavcodec
 * (libopencore_amrnb pour l'AMR-NB, libvo_amrwbenc pour l'AMR-WB) ; le décodage
 * utilise les décodeurs natifs ffmpeg. Plus de dépendance directe -lopencore-amr*.
 */
#ifndef AMRCODEC_H
#define	AMRCODEC_H

#include "../ffaudiocodec.h"

/**
 * AMR-NB : 8 kHz mono, trame 20 ms (160 échantillons), horloge RTP 8 kHz.
 */
class AMRNBEncoder : public FfAudioEncoder
{
public:
	AMRNBEncoder(const Properties &properties);
};

class AMRNBDecoder : public FfAudioDecoder
{
public:
	AMRNBDecoder();

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_AMR_NB); }
};

/**
 * AMR-WB : 16 kHz mono, trame 20 ms (320 échantillons), horloge RTP 16 kHz.
 */
class AMRWBEncoder : public FfAudioEncoder
{
public:
	AMRWBEncoder(const Properties &properties);
};

class AMRWBDecoder : public FfAudioDecoder
{
public:
	AMRWBDecoder();

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_AMR_WB); }
};

#endif	/* AMRCODEC_H */
