#include "speexcodec.h"
#include <medkit/log.h>

/****************************** SpeexEncoder ********************************/

SpeexEncoder::SpeexEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_SPEEX, AudioCodec::SPEEX16)
{
	// Speex wideband : 16000 Hz, trame 20 ms = 160 échantillons.
	defaultSampleRate = 16000;

	// Qualité 0-10 (défaut 5 = ~16.8 kbit/s en wideband).
	// ctx->compression_level est lu par l'encodeur libspeex de ffmpeg via
	// SPEEX_SET_QUALITY avant ouverture du codec.
	if (ctx)
		ctx->compression_level = properties.GetProperty("speex.quality", 5);

	TrySetRate(16000);
	Open();
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}

/****************************** SpeexDecoder ********************************/

SpeexDecoder::SpeexDecoder() :
	FfAudioDecoder(AV_CODEC_ID_SPEEX, AudioCodec::SPEEX16)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}
