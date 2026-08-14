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
	// 16000 : sans extradata, le décodeur speex natif de ffmpeg exige la
	// fréquence sur le contexte avant l'ouverture — sinon avcodec_open2 échoue
	// et chaque trame reçue meurt sur « decoder not opened ».
	FfAudioDecoder(AV_CODEC_ID_SPEEX, AudioCodec::SPEEX16, nullptr, 0, nullptr, 16000)
{
	if (numFrameSamples <= 0)
		numFrameSamples = 160;
}
