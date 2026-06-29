/*
 * File:   g722codec.cpp
 * Author: Emmanuel BUU
 *
 * G.722 branché sur la base générique FfAudioEncoder/FfAudioDecoder (ffmpeg 5).
 */
#include "g722codec.h"
#include <medkit/log.h>

G722Encoder::G722Encoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_ADPCM_G722, AudioCodec::G722)
{
	// G.722 natif : S16 mono 16 kHz (pas de rééchantillonnage attendu).
	defaultSampleRate = 16000;
	TrySetRate(16000);
	Open();

	// L'encodeur ADPCM G.722 expose frame_size = 320 (20 ms @ 16 kHz) ;
	// repli défensif si jamais il restituait une trame variable.
	if (numFrameSamples <= 0)
		numFrameSamples = 20 * 16;
}

G722Decoder::G722Decoder() :
	FfAudioDecoder(AV_CODEC_ID_ADPCM_G722, AudioCodec::G722)
{
	// Décodeur à trame variable : on restitue des tranches de 20 ms @ 16 kHz.
	if (numFrameSamples <= 0)
		numFrameSamples = 20 * 16;
}
