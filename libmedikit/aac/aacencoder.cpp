/*
 * File:   aacencoder.cpp
 * Author: Sergio
 *
 * Created on 20 de junio de 2013, 10:46
 *
 * Porté sur la base générique FfAudioEncoder (ffmpeg 5).
 */
#include "aacencoder.h"
#include <medkit/log.h>

AACEncoder::AACEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_AAC, AudioCodec::AAC)
{
	// La base a échoué à trouver/allouer le codec.
	if (!ctx)
		return;

	DWORD rate = (DWORD)properties.GetProperty("aac.samplerate", 8000);

	// Repli si la fréquence demandée n'est pas supportée nativement.
	defaultSampleRate = rate;

	// Débit et tolérance à régler AVANT l'ouverture du codec.
	ctx->bit_rate = properties.GetProperty("aac.bitrate", (int)(rate * 3));
	ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	// L'encodeur AAC natif ne sort qu'en FLTP : TrySetRate() installe le
	// rééchantillonneur S16 -> FLTP (même fréquence, produced == inLen).
	TrySetRate(rate);

	if (!Open())
		Error("AAC: could not open encoder\n");
}
