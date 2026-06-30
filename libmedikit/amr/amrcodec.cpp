/*
 * File:   amrcodec.cpp
 *
 * AMR-NB / AMR-WB sur la base générique FfAudioEncoder/FfAudioDecoder (ffmpeg).
 */
#include "amrcodec.h"
#include <medkit/log.h>

/******************************** AMR-NB ********************************/

AMRNBEncoder::AMRNBEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_AMR_NB, AudioCodec::AMR)
{
	// AMR-NB : S16 mono 8 kHz, trame 20 ms.
	defaultSampleRate = 8000;
	TrySetRate(8000);

	// L'encodeur AMR-NB exige un débit correspondant à un mode valide.
	// 12,2 kbit/s (MR122), le mode le plus courant.
	if (ctx)
		ctx->bit_rate = 12200;

	Open();

	// frame_size attendu = 160 (20 ms @ 8 kHz) ; repli défensif.
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}

AMRNBDecoder::AMRNBDecoder() :
	FfAudioDecoder(AV_CODEC_ID_AMR_NB, AudioCodec::AMR)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}

/******************************** AMR-WB ********************************/

AMRWBEncoder::AMRWBEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_AMR_WB, AudioCodec::AMRWB)
{
	// AMR-WB : S16 mono 16 kHz, trame 20 ms.
	defaultSampleRate = 16000;
	TrySetRate(16000);

	// Débit correspondant à un mode AMR-WB valide : 23,85 kbit/s (mode le plus haut).
	if (ctx)
		ctx->bit_rate = 23850;

	Open();

	// frame_size attendu = 320 (20 ms @ 16 kHz) ; repli défensif.
	if (numFrameSamples <= 0)
		numFrameSamples = 320;
}

AMRWBDecoder::AMRWBDecoder() :
	FfAudioDecoder(AV_CODEC_ID_AMR_WB, AudioCodec::AMRWB)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 320;
}
