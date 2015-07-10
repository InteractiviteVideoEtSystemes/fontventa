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
	
	encoder = VideoCodecFactory::CreateEncoder(codec, properties);
	
	if (encoder != NULL)
	{
		HandleSizeChange();
		return true;
	}
	return false;
}


bool PictureStreamer::SetFrameRate(int fps,int kbits,int intraPeriod)
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
		Error("-PictureStreamer: no picture loaded. Cannot stream.\n");
		return NULL;
	}
	
	if (encoder == NULL)
	{
		// No encoder
		Error("-PictureStreamer: no video encoder configured. Cannot stream.\n");
		return NULL;
	}
	
	VideoFrame * f = encoder->EncodeFrame( GetFrame(), (GetWidth()*GetHeight()*3)/2 );
	
	if (f == NULL) Error("-PictureStreamer: fail to encode picture. Cannot stream.\n");
	
	return f;
}

bool PictureStreamer::HandleSizeChange()
{
	if (GetWidth() == 0 || GetHeight() == 0) return false;
	
	if (encoder)
	{
		return encoder->SetSize(GetWidth(), GetHeight());
	}
	else
	{
		return true;
	}
}
