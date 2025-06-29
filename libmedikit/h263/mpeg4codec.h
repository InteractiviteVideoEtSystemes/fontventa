#ifndef _MPEG4CODEC_H_
#define _MPEG4CODEC_H_

#include "../ffvideocodec.h"

class Mpeg4Encoder : public FfVideoEncoder
{
public:
	Mpeg4Encoder(const Properties& properties);
	virtual ~Mpeg4Encoder();
};

class Mpeg4Decoder : public FfVideoDecoder
{
public:
	Mpeg4Decoder();
	virtual ~Mpeg4Decoder();
};

#endif
