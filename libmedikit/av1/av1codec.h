#ifndef _MPEG4CODEC_H_
#define _MPEG4CODEC_H_

#include "../ffvideocodec.h"

class AV1Encoder : public FfVideoEncoder
{
public:
	AV1Encoder(const Properties& properties);
	virtual ~AV1Encoder();
};

class AV1Decoder : public FfVideoDecoder
{
public:
	AV1Decoder();
	virtual ~AV1Decoder();
};

#endif
