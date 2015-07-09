#include "medkit/logo.h"
#include "medkit/video.h"

#ifndef PICTURESTREAMER_H
#define PICTURESTREAMER_H

class PictureStreamer : public Logo
{
public:
	PictureStreamer();
	virtual ~PictureStreamer();

	/**
	 * Create an encoder and configure it to stream the pricture
	 **/
	
	int Load(const char *filename, unsigned int pwidth = 0, unsigned int pheight = 0)
	{
		if (Logo::Load(filename, pwidth, pheight))
		{
			if (HandleSizeChange() ) return 1;
		}
		return 0;
	}
	
	/**
	 * Create an encoder and configure it to stream the pricture
	 **/
	bool SetCodec(VideoCodec::Type codec, const Properties &properties);

	/**
	 * Set Frame rate of the encoder
	 */
	 
	bool SetFrameRate(int fps,int kbits,int intraPeriod);
	
	/**
	 * Create an encoded videoframe
	 **/
	VideoFrame* Stream(bool askiframe = false);
	
private:
	VideoEncoder * encoder;
	
	bool HandleSizeChange();
}

#endif