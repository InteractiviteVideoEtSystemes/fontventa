/*
 * File:   aacdecoder.h
 *
 * Décodeur AAC basé sur la base générique FfAudioDecoder (ffmpeg 5), pendant
 * de AACEncoder. L'AAC des MP4 est du « raw » (sans en-tête ADTS) : il ne se
 * décode qu'avec son AudioSpecificConfig, transmis en extradata (esds).
 */

#ifndef AACDECODER_H
#define	AACDECODER_H

#include "../ffaudiocodec.h"

class AACDecoder : public FfAudioDecoder
{
public:
	AACDecoder(const BYTE* extradata = NULL, int extradataSize = 0);

	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_AAC); }
};

#endif	/* AACDECODER_H */
