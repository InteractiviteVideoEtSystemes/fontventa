/*
 * GSM-FR via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 */
#include "gsmcodec.h"
#include <medkit/log.h>

GSMEncoder::GSMEncoder(const Properties &properties) :
	// Pas d'encodeur GSM natif dans ffmpeg : avcodec_find_encoder(AV_CODEC_ID_GSM)
	// retourne le wrapper "libgsm" (seul encodeur disponible). Le décodeur "gsm"
	// est lui natif (sélectionné automatiquement par FfAudioDecoder).
	FfAudioEncoder(properties, AV_CODEC_ID_GSM, AudioCodec::GSM)
{
	// GSM-FR : S16 mono 8000 Hz, trame 20 ms (160 échantillons), 33 octets.
	defaultSampleRate = 8000;
	TrySetRate(8000);
	Open();
	// frame_size = 160 pour GSM-FR ; repli défensif.
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}

GSMDecoder::GSMDecoder() :
	FfAudioDecoder(AV_CODEC_ID_GSM, AudioCodec::GSM)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}
