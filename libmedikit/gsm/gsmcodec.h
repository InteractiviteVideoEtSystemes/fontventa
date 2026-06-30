/*
 * GSM-FR (Berlin toast format) via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 *
 * ffmpeg fournit un encodeur « libgsm » (wrapper libgsm.so, lié
 * dynamiquement dans libavcodec.so) et un décodeur natif « gsm ».
 * AV_CODEC_ID_GSM = 8000 Hz mono, trame 20 ms (160 échantillons), 33 octets.
 * Plus de dépendance directe -lgsm : libgsm.so est tiré transitivement
 * via libavcodec.so.
 */
#ifndef GSMCODEC_H
#define GSMCODEC_H

#include "../ffaudiocodec.h"

class GSMEncoder : public FfAudioEncoder
{
public:
	GSMEncoder(const Properties &properties);
	virtual ~GSMEncoder() {}
};

class GSMDecoder : public FfAudioDecoder
{
public:
	GSMDecoder();
	virtual ~GSMDecoder() {}
};

#endif /* GSMCODEC_H */
