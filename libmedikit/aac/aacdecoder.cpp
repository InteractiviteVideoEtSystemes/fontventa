/*
 * File:   aacdecoder.cpp
 *
 * Décodeur AAC sur la base générique FfAudioDecoder (ffmpeg 5).
 */
#include "aacdecoder.h"

AACDecoder::AACDecoder(const BYTE* extradata, int extradataSize) :
	FfAudioDecoder(AV_CODEC_ID_AAC, AudioCodec::AAC, (const uint8_t*)extradata, extradataSize)
{
}
