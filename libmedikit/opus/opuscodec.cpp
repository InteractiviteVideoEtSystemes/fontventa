/*
 * OPUS via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 */
#include "opuscodec.h"
#include <medkit/log.h>

/******************************** OPUSEncoder ********************************/

OPUSEncoder::OPUSEncoder(const Properties &properties) :
	// "opus" = encodeur natif ffmpeg (priorité sur le wrapper "libopus" que
	// avcodec_find_encoder(AV_CODEC_ID_OPUS) retournerait par défaut).
	FfAudioEncoder(properties, AV_CODEC_ID_OPUS, AudioCodec::OPUS, "opus")
{
	// Opus opère toujours à 48000 Hz dans le pipeline MCU (horloge RTP fixe).
	defaultSampleRate = 48000;
	TrySetRate(48000);
	Open();
	// frame_size pour 20 ms à 48000 Hz = 960 ; repli défensif si variable.
	if (numFrameSamples <= 0)
		numFrameSamples = 960;
}

DWORD OPUSEncoder::TrySetRate(DWORD /*rate*/)
{
	// L'horloge RTP d'OPUS est fixée à 48000 Hz (RFC 7587).
	// On force toujours 48000 Hz, quelle que soit la fréquence demandée par
	// le MCU. GetRate() retourne alors 48000 et le MCU adapte sa cadence.
	return FfAudioEncoder::TrySetRate(48000);
}

/******************************** OPUSDecoder ********************************/

OPUSDecoder::OPUSDecoder() :
	FfAudioDecoder(AV_CODEC_ID_OPUS, AudioCodec::OPUS)
{
	// Le décodeur natif ffmpeg OPUS produit du PCM 48000 Hz.
	if (numFrameSamples <= 0)
		numFrameSamples = 960;
}
