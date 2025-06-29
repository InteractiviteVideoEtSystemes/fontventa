#ifndef _H264DECODER_H_
#define _H264DECODER_H_

#include "../ffvideocodec.h"

class H264Decoder : public FfVideoDecoder
{
public:
	H264Decoder();
	virtual ~H264Decoder();
};
#endif
