#include "medkit/picturestreamer.h"
#include "medkit/log.h"


PictureStreamer::PictureStreamer() : Logo()
{
	encoder = NULL;
}

PictureStreamer::~PictureStreamer()
{
	if (encoder) delete encoder;
}


bool PictureStreamer::SetCodec(VideoCodec::Type codec, const Properties &properties)
{
	if (encoder) delete encoder;
	
	encoder = VideoCodecFactory(codec, properties);
}


bool PictureStreamer::SetFrameRate(int fps,int kbits,int intraPeriod);
{
	if (encoder)
	{
		return encoder->SetFrameRate(fps, kbits, intraPeriod);
	}
	
	return false;
}

VideoFrame* PictureStreamer::Stream(bool askiframe)
{
	if ( GetFrame() == NULL)
	{
		// No picture loaded
		return NULL;
	}
	
	if (encoder == NULL)
	{
		// No encoder
		return NULL;
	}
	
	return encoder->EncodeFrame( GetFrame(), GetSize() );
}

