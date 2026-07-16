#include "medkit/log.h"
#include "medkit/video.h"
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

// ---------------------------------------------------------------------------
// Fabriques d'images fixes (remplacent l'ancienne classe Logo, cf. avframe.md).
// Un chargement/une création produit un Pict IMMUABLE refcompté.
// ---------------------------------------------------------------------------

PictPtr Pict::CreateColor(int width, int height, BYTE y, BYTE u, BYTE v)
{
	if (width <= 0 || height <= 0)
		return nullptr;

	AVFrame* f = av_frame_alloc();
	if (!f)
		return nullptr;
	f->format = AV_PIX_FMT_YUV420P;
	f->width  = width;
	f->height = height;
	if (av_frame_get_buffer(f, 32) < 0)
	{
		av_frame_free(&f);
		return nullptr;
	}

	// Remplissage uni des trois plans (YUV420P : plans U/V à hauteur/2).
	memset(f->data[0], y, f->linesize[0] * height);
	memset(f->data[1], u, f->linesize[1] * (height / 2));
	memset(f->data[2], v, f->linesize[2] * (height / 2));

	return std::make_shared<Pict>(f);
}

PictPtr Pict::CreateBlack(int width, int height)
{
	// Noir : Y=0, U=V=128.
	return CreateColor(width, height, 0, 128, 128);
}

PictPtr Pict::Load(const char* fileName, int pwidth, int pheight)
{
	AVFormatContext *fctx = NULL;
	AVCodecContext  *ctx  = NULL;
	const AVCodec   *codec = NULL;
	AVCodecParameters *par = NULL;
	AVFrame  *logoRGB = NULL;
	AVFrame  *out     = NULL;
	SwsContext *sws   = NULL;
	AVPacket *packet  = NULL;
	PictPtr   result;
	int width = 0, height = 0;

	//Create context from file
	if (avformat_open_input(&fctx, fileName, NULL, NULL) < 0)
	{
		Error("Couldn't open the logo image file [%s]\n", fileName);
		return nullptr;
	}

	if (avformat_find_stream_info(fctx, NULL) < 0)
	{
		Error("Couldn't find stream information for the image file...\n");
		goto end;
	}

	//Get stream parameters (AVStream::codec supprimé en ffmpeg >= 5)
	par = fctx->streams[0]->codecpar;

	if (!(codec = avcodec_find_decoder(par->codec_id)))
	{
		Error("Couldn't find codec for the image file...\n");
		goto end;
	}

	if (!(ctx = avcodec_alloc_context3(codec)))
	{
		Error("Couldn't alloc codec context\n");
		goto end;
	}
	if (avcodec_parameters_to_context(ctx, par) < 0)
	{
		Error("Couldn't copy codec parameters to context\n");
		goto end;
	}
	if (avcodec_open2(ctx, codec, NULL) < 0)
	{
		Error("Couldn't open codec for the image file...\n");
		goto end;
	}

	if (!(packet = av_packet_alloc()))
	{
		Error("Couldn't alloc packet\n");
		goto end;
	}
	if (av_read_frame(fctx, packet) < 0)
	{
		Error("Couldn't read frame from the image file...\n");
		goto end;
	}
	if (!(logoRGB = av_frame_alloc()))
	{
		Error("Couldn't alloc frame\n");
		goto end;
	}

	//Decode: send packet then receive frame
	if (avcodec_send_packet(ctx, packet) < 0)
	{
		Error("Couldn't send packet to decoder\n");
		goto end;
	}
	if (avcodec_receive_frame(ctx, logoRGB) < 0)
	{
		Error("Couldn't decode image\n");
		goto end;
	}

	//Compute target size (native, or keep aspect if only one dimension given)
	if (pwidth > 0)
	{
		width = pwidth;
		height = (pheight > 0) ? pheight : (ctx->height * pwidth) / ctx->width;
	}
	else if (pheight > 0)
	{
		height = pheight;
		width = (ctx->width * pheight) / ctx->height;
	}
	else
	{
		width = ctx->width;
		height = ctx->height;
	}
	width  &= ~1;   // pair (YUV420)
	height &= ~1;
	if (width <= 0)  width = 2;
	if (height <= 0) height = 2;

	//Destination frame (owned)
	if (!(out = av_frame_alloc()))
	{
		Error("Couldn't alloc output frame\n");
		goto end;
	}
	out->format = AV_PIX_FMT_YUV420P;
	out->width  = width;
	out->height = height;
	if (av_frame_get_buffer(out, 32) < 0)
	{
		Error("Couldn't alloc output frame buffer\n");
		av_frame_free(&out);
		out = NULL;
		goto end;
	}

	//Create YUV rescaler context
	if (!(sws = sws_getContext(ctx->width, ctx->height, ctx->pix_fmt,
	                           width, height, AV_PIX_FMT_YUV420P,
	                           SWS_FAST_BILINEAR, NULL, NULL, NULL)))
	{
		Error("Couldn't alloc sws context\n");
		av_frame_free(&out);
		out = NULL;
		goto end;
	}

	//Convert
	sws_scale(sws, logoRGB->data, logoRGB->linesize, 0, ctx->height, out->data, out->linesize);

	result = std::make_shared<Pict>(out);
	out = NULL;   // possédé par le Pict désormais

end:
	if (packet)  av_packet_free(&packet);
	if (logoRGB) av_frame_free(&logoRGB);
	if (out)     av_frame_free(&out);
	if (ctx)     avcodec_free_context(&ctx);
	if (sws)     sws_freeContext(sws);
	if (fctx)    avformat_close_input(&fctx);

	return result;
}
