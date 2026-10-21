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

	// L'encodeur AMR-NB exige un débit correspondant à un mode valide. Le plus
	// haut que la négociation permet (`amr.mode`, RFC 4867 mode-set), 12,2 kbit/s
	// (MR122) par défaut.
	static const int nbRates[] = { 4750, 5150, 5900, 6700, 7400, 7950, 10200, 12200 };
	const int nbMode = properties.GetProperty("amr.mode", 7);
	if (ctx)
		ctx->bit_rate = nbRates[nbMode >= 0 && nbMode <= 7 ? nbMode : 7];

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

	// Débit correspondant à un mode AMR-WB valide : le plus haut que la
	// négociation permet (`amrwb.mode`, RFC 4867 mode-set), 23,85 kbit/s par défaut.
	static const int wbRates[] = { 6600, 8850, 12650, 14250, 15850, 18250, 19850, 23050, 23850 };
	const int wbMode = properties.GetProperty("amrwb.mode", 8);
	if (ctx)
		ctx->bit_rate = wbRates[wbMode >= 0 && wbMode <= 8 ? wbMode : 8];

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
