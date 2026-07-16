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
	BYTE* buf = GetFrame();
	if ( buf == NULL)
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

	const int w = GetWidth();
	const int h = GetHeight();

	// Enveloppe le buffer YUV420P contigu du logo dans un AVFrame NON refcompté
	// (data pointe dans le buffer, aucun ownership : buf[] restent nuls, donc
	// av_frame_free ne libère pas le buffer du logo). L'encodeur en fera une copie
	// interne (av_frame_ref recopie une trame non refcomptée).
	AVFrame* f = av_frame_alloc();
	if (!f) return NULL;
	f->format = AV_PIX_FMT_YUV420P;
	f->width  = w;
	f->height = h;
	f->data[0] = buf;
	f->data[1] = buf + w*h;
	f->data[2] = buf + w*h*5/4;
	f->linesize[0] = w;
	f->linesize[1] = w/2;
	f->linesize[2] = w/2;

	PictPtr pic = std::make_shared<Pict>(f);

	VideoFrame * vf = encoder->EncodeFrame( pic );

	if (vf == NULL) Error("-PictureStreamer: fail to encode picture. Cannot stream.\n");

	return vf;
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
