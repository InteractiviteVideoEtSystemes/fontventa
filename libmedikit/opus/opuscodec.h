/*
 * OPUS via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 *
 * L'horloge RTP d'OPUS est TOUJOURS 48000 Hz (RFC 7587), quelle que soit
 * la fréquence de travail du pipeline MCU. L'encodeur force ctx->sample_rate
 * à 48000 Hz ; le MCU voit GetRate()=48000 et envoie 960 échantillons/trame
 * (20 ms). Plus de dépendance directe -lopus : libopus.so est tiré
 * transitivement via libavcodec.so.
 */
#ifndef OPUSCODEC_H
#define OPUSCODEC_H

#include "../ffaudiocodec.h"

/**
 * Encodeur OPUS : toujours à 48000 Hz. TrySetRate() ignore le taux demandé
 * et retourne 48000, afin que GetRate()/GetClockRate() soient cohérents avec
 * l'horloge RTP fixe d'OPUS.
 */
class OPUSEncoder : public FfAudioEncoder
{
public:
	OPUSEncoder(const Properties &properties);
	virtual ~OPUSEncoder() {}

	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetClockRate() { return 48000; }
};

/**
 * Décodeur OPUS : produit du PCM 48000 Hz. GetRate()/TrySetRate() retournent
 * 48000 statiquement (le décodeur natif ffmpeg est initialisé à 48000 Hz).
 */
class OPUSDecoder : public FfAudioDecoder
{
public:
	OPUSDecoder();
	virtual ~OPUSDecoder() {}

	virtual DWORD GetRate()         { return 48000; }
	virtual DWORD TrySetRate(DWORD) { return 48000; }
};

#endif /* OPUSCODEC_H */
