#ifndef _FFVIDEOCODEC_H_
#define _FFVIDEOCODEC_H_
extern "C" {
#include <libavcodec/avcodec.h>
}

#include "medkit/codecs.h"
#include "medkit/video.h"
#include <list>

class FfVideoEncoder : public VideoEncoder
{
public:
	FfVideoEncoder(const Properties& properties, enum AVCodecID av_codec, enum VideoCodec::Type codec_id);
	virtual ~FfVideoEncoder();
	virtual VideoFrame* EncodeFrame(BYTE *in, DWORD len);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width, int height);
	virtual int SetFrameRate(int fps, int kbits, int intraPeriod);

private:
	int OpenCodec();

	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVFrame		*picture;
	int		bitrate;
	int		fps;
	int		format;
	int		opened;
	int		intraPeriod;
	VideoFrame	*frame;
};

class FfVideoDecoder : public VideoDecoder
{
public:
	FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id);
	virtual ~FfVideoDecoder();
	virtual int Decode(BYTE *in,DWORD len);
	virtual int GetWidth()		{ return ctx->width;		};
	virtual int GetHeight()		{ return ctx->height;		};
	virtual BYTE* GetFrame()	{ return (BYTE *)frame;		};
	virtual bool  IsKeyFrame()	{ return picture->key_frame;	};
private:
	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVCodecParserContext *parser_ctx;
	AVFrame		*picture;
	BYTE*		buffer;
	DWORD		bufLen;
	DWORD 		bufSize;
	BYTE*		frame;
	DWORD		frameSize;
	BYTE		src;
};

#endif
