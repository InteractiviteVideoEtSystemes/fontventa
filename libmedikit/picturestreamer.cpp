#include "medkit/picturestreamer.h"
#include "medkit/log.h"


PictureStreamer::PictureStreamer()
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

VideoFramePtr PictureStreamer::Stream(bool askiframe)
{
	if (!pict)
	{
		// No picture loaded
		Error("-PictureStreamer: no picture loaded. Cannot stream.\n");
		return nullptr;
	}

	if (encoder == NULL)
	{
		// No encoder
		Error("-PictureStreamer: no video encoder configured. Cannot stream.\n");
		return nullptr;
	}

	// Un encodeur MATÉRIEL retient sa première image : EncodeFrame rend alors
	// nullptr sans que rien n'aille mal. L'image d'un PictureStreamer étant FIXE,
	// la représenter ne change rien à ce qui sortira. Sans cela, ni le prologue
	// vidéo ni le logo ne produisent la moindre trame dès que le GPU encode — le
	// prologue annonçait « 0 trame noire » pour un délai de 357 ms.
	VideoFramePtr vf;
	for (int attempt = 0; attempt < 4 && vf == NULL; ++attempt)
		vf = encoder->EncodeFrame( pict );

	if (vf == NULL) Error("-PictureStreamer: fail to encode picture. Cannot stream.\n");

	return vf;
}

bool PictureStreamer::HandleSizeChange()
{
	if (!pict || pict->GetWidth() == 0 || pict->GetHeight() == 0)
		return false;

	if (encoder)
	{
		return encoder->SetSize(pict->GetWidth(), pict->GetHeight());
	}
	else
	{
		return true;
	}
}
