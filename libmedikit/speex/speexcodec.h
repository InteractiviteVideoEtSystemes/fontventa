#ifndef _SPEEXCODEC_H_
#define _SPEEXCODEC_H_
#include "../ffaudiocodec.h"

// Speex 16 kHz (wideband) via libavcodec.
// Encodeur : wrapper "libspeex" de ffmpeg (seul disponible).
// Décodeur : décodeur natif ffmpeg AV_CODEC_ID_SPEEX.
// Fréquence : 16000 Hz fixe (mode wideband uniquement), trame 20 ms = 320
// échantillons — surtout pas les 160 du monde 8 kHz.

class SpeexEncoder : public FfAudioEncoder
{
public:
	SpeexEncoder(const Properties &properties);
	virtual ~SpeexEncoder() {}
	// Force 16000 Hz quel que soit le taux demandé par le MCU.
	virtual DWORD TrySetRate(DWORD)  { return FfAudioEncoder::TrySetRate(16000); }
	virtual DWORD GetClockRate()     { return 16000; }

	static bool IsSupported() { return FfAudioEncoder::IsCodecAvailable(AV_CODEC_ID_SPEEX); }
};

class SpeexDecoder : public FfAudioDecoder
{
public:
	SpeexDecoder();
	virtual ~SpeexDecoder() {}
	virtual DWORD GetRate()          { return 16000; }
	virtual DWORD TrySetRate(DWORD)  { return 16000; }

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_SPEEX); }
};

#endif
