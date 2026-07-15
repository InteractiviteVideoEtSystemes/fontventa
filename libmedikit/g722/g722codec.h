#ifndef G722_H
#define	G722_H

#include "../ffaudiocodec.h"

/**
 * Encodeur G.722 : ADPCM 16 kHz mono, adossé à la base générique ffmpeg.
 */
class G722Encoder : public FfAudioEncoder
{
public:
	G722Encoder(const Properties &properties);

	// G.722 : audio échantillonné à 16 kHz mais horloge RTP annoncée à 8 kHz
	// (RFC 3551 §4.5.2).
	virtual DWORD GetClockRate()	{ return 8000; }
};

/**
 * Décodeur G.722.
 */
class G722Decoder : public FfAudioDecoder
{
public:
	G722Decoder();

	// Restitution fixée à 16 kHz (fréquence native du flux G.722).
	virtual DWORD GetRate()		{ return 16000; }

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_ADPCM_G722); }
};

#endif	/* G722_H */
