#ifndef _G711CODEC_H_
#define _G711CODEC_H_

#include "../ffaudiocodec.h"

/**
 * G.711 (RFC 3551) adossé à la base générique ffmpeg, comme tous les autres
 * codecs de la bibliothèque : PCM 8 kHz mono, un octet par échantillon.
 *
 * Les encodeurs/décodeurs PCM de ffmpeg n'annoncent pas de frame_size : c'est
 * FfAudioEncoder::Open() qui impose alors la tranche de 20 ms (160 échantillons),
 * la granularité qu'attend la packetisation RTP.
 */
class PCMAEncoder : public FfAudioEncoder
{
public:
	PCMAEncoder(const Properties &properties);

	virtual DWORD GetClockRate()	{ return 8000; }

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_PCM_ALAW); }
};

class PCMADecoder : public FfAudioDecoder
{
public:
	PCMADecoder();

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_PCM_ALAW); }
};

class PCMUEncoder : public FfAudioEncoder
{
public:
	PCMUEncoder(const Properties &properties);

	virtual DWORD GetClockRate()	{ return 8000; }

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_PCM_MULAW); }
};

class PCMUDecoder : public FfAudioDecoder
{
public:
	PCMUDecoder();

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_PCM_MULAW); }
};

#endif
