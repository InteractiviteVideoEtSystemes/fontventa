#ifndef _H263CODEC_H_
#define _H263CODEC_H_

#include "../medkit/h263packet.h"
#include "../ffvideocodec.h"
#include <list>

class H263Encoder : public FfVideoEncoder
{
public:
	H263Encoder(const Properties& properties);
	virtual ~H263Encoder();
};

class H263Decoder : public FfVideoDecoder
{
public:
	H263Decoder();
	virtual ~H263Decoder();
};

#endif
