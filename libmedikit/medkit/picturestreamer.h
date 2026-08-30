#include "medkit/video.h"

#ifndef PICTURESTREAMER_H
#define PICTURESTREAMER_H

// Encode et diffuse une image fixe (avatar, logo...). N'hérite plus de Logo
// (supprimé) : détient un Pict produit par Pict::Load / Pict::CreateBlack.
class PictureStreamer
{
public:
	PictureStreamer();
	virtual ~PictureStreamer();

	int Load(const char *filename, unsigned int pwidth = 0, unsigned int pheight = 0)
	{
		pict = Pict::Load(filename, pwidth, pheight);
		if (pict && HandleSizeChange())
			return 1;
		return 0;
	}

	void PaintBlackRectangle(unsigned int pwidth, unsigned int pheight)
	{
		pict = Pict::CreateBlack(pwidth, pheight);
		HandleSizeChange();
	}

	/**
	 * Create an encoder and configure it to stream the picture
	 **/
	bool SetCodec(VideoCodec::Type codec, const Properties &properties);

	/**
	 * Set Frame rate of the encoder
	 */
	bool SetFrameRate(int fps,int kbits,int intraPeriod);

	/**
	 * Create an encoded videoframe
	 **/
	VideoFramePtr Stream(bool askiframe = false);

private:
	PictPtr pict;
	VideoEncoder * encoder;

	bool HandleSizeChange();
};

#endif
