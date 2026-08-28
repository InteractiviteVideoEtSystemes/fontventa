/*
 * File:   nellycodec.h
 *
 * NellyMoser Asao 8 kHz et 11,025 kHz via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 * ffmpeg expose AV_CODEC_ID_NELLYMOSER en encodeur ET décodeur.
 * Le codec travaille en AV_SAMPLE_FMT_FLT ; le resampler S16->FLT est créé
 * automatiquement par FfAudioEncoder::TrySetRate().
 */
#ifndef _NELLYCODEC_H_
#define _NELLYCODEC_H_

#include "../ffaudiocodec.h"

/* ---- Encodeurs ---- */

class NellyEncoder : public FfAudioEncoder
{
public:
	NellyEncoder(const Properties &properties);
	virtual ~NellyEncoder() {}

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_NELLYMOSER); }
};

class NellyEncoder11Khz : public FfAudioEncoder
{
public:
	NellyEncoder11Khz(const Properties &properties);
	virtual ~NellyEncoder11Khz() {}

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_NELLYMOSER); }
};

/* ---- Décodeurs ---- */

class NellyDecoder : public FfAudioDecoder
{
public:
	NellyDecoder();
	virtual ~NellyDecoder() {}
	// GetRate() surchargé : ctx->sample_rate est 0 avant le 1er paquet car
	// NellyMoser embarque la fréquence dans le flux ; on retourne la valeur fixe.
	virtual DWORD GetRate() { return 8000; }
	virtual DWORD TrySetRate(DWORD) { return 8000; }

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_NELLYMOSER); }
};

class NellyDecoder11Khz : public FfAudioDecoder
{
public:
	NellyDecoder11Khz();
	virtual ~NellyDecoder11Khz() {}
	virtual DWORD GetRate() { return 11025; }
	virtual DWORD TrySetRate(DWORD) { return 11025; }

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_NELLYMOSER); }
};

#endif
