/*
 * File:   nellycodec.cpp
 *
 * NellyMoser Asao 8 kHz et 11,025 kHz via FfAudioEncoder/FfAudioDecoder.
 */
#include "nellycodec.h"
#include <medkit/log.h>

/******************************** Encodeurs ********************************/

NellyEncoder::NellyEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_NELLYMOSER, AudioCodec::NELLY8)
{
	// NellyMoser 8 kHz. Le codec impose AV_SAMPLE_FMT_FLT ; le resampler
	// S16->FLT est créé automatiquement dans TrySetRate().
	defaultSampleRate = 8000;
	TrySetRate(8000);
	Open();

	// frame_size = 256 pour NellyMoser ; repli défensif si non renseigné.
	if (numFrameSamples <= 0)
		numFrameSamples = 256;
}

NellyEncoder11Khz::NellyEncoder11Khz(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_NELLYMOSER, AudioCodec::NELLY11)
{
	defaultSampleRate = 11025;
	TrySetRate(11025);
	Open();

	if (numFrameSamples <= 0)
		numFrameSamples = 256;
}

/******************************** Décodeurs ********************************/

NellyDecoder::NellyDecoder() :
	FfAudioDecoder(AV_CODEC_ID_NELLYMOSER, AudioCodec::NELLY8)
{
	// NellyMoser porte la fréquence dans le flux ; ctx->sample_rate = 0 après
	// open (avant le 1er paquet). GetRate() retourne 8000 statiquement.
	if (numFrameSamples <= 0)
		numFrameSamples = 256;
}

NellyDecoder11Khz::NellyDecoder11Khz() :
	FfAudioDecoder(AV_CODEC_ID_NELLYMOSER, AudioCodec::NELLY11)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 256;
}
