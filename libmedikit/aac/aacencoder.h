/*
 * File:   aacencoder.h
 * Author: Sergio
 *
 * Created on 20 de junio de 2013, 10:46
 *
 * Porté sur la base générique FfAudioEncoder (ffmpeg 5).
 */

#ifndef AACENCODER_H
#define	AACENCODER_H

#include "../ffaudiocodec.h"

/**
 * Encodeur AAC : l'encodeur natif ffmpeg ne sort qu'en FLTP, la base met donc
 * en place le rééchantillonneur S16 -> FLTP (même fréquence). Trame fixe de
 * 1024 échantillons.
 */
class AACEncoder : public FfAudioEncoder
{
public:
	AACEncoder(const Properties &properties);

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_AAC); }
};

#endif	/* AACENCODER_H */
