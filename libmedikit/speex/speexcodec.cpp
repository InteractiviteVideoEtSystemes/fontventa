#include "speexcodec.h"
#include <medkit/log.h>

/****************************** SpeexEncoder ********************************/

SpeexEncoder::SpeexEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_SPEEX, AudioCodec::SPEEX16)
{
	// Speex wideband : 16000 Hz, trame 20 ms = 320 échantillons.
	defaultSampleRate = 16000;

	// Qualité 0-10 (défaut 5 = ~16.8 kbit/s en wideband).
	// ctx->compression_level est lu par l'encodeur libspeex de ffmpeg via
	// SPEEX_SET_QUALITY avant ouverture du codec.
	if (ctx)
		ctx->compression_level = properties.GetProperty("speex.quality", 5);

	TrySetRate(16000);
	Open();
	// frame_size attendu = 320 (20 ms @ 16 kHz) ; repli défensif.
	if (numFrameSamples <= 0)
		numFrameSamples = 20 * 16;
}

/****************************** SpeexDecoder ********************************/

SpeexDecoder::SpeexDecoder() :
	// 16000 : sans extradata, le décodeur speex natif de ffmpeg exige la
	// fréquence sur le contexte avant l'ouverture — sinon avcodec_open2 échoue
	// et chaque trame reçue meurt sur « decoder not opened ».
	FfAudioDecoder(AV_CODEC_ID_SPEEX, AudioCodec::SPEEX16, nullptr, 0, nullptr, 16000)
{
	// Le décodeur speex natif de ffmpeg laisse ctx->frame_size à 0 (trace
	// « [speex] decoder open: frame size 0, 16000 Hz ») : ce repli est le
	// seul chiffre qui pilote la restitution, et Decode() rend EXACTEMENT
	// numFrameSamples échantillons par appel, le reste attendant en fifo.
	// À 160 — la valeur 8 kHz recopiée ici — une trame wideband de 320
	// échantillons ne ressortait qu'à moitié : 8000 éch/s au lieu de 16000
	// vers le pipe, donc 25 paquets/s au lieu de 50 sur la patte opus, la
	// fifo du décodeur saturée à 8192 (+512 ms de latence) et une trame sur
	// deux jetée par push(). Audio haché mesuré en capture le 2026-08-14
	// (capture16.pcap, sens speex16 -> opus).
	if (numFrameSamples <= 0)
		numFrameSamples = 20 * 16;
}
