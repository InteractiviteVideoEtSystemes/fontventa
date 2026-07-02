#include "medkit/log.h"
#include "medkit/logo.h"
#include <stdlib.h>
extern "C" {
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

Logo::Logo()
{
	//No logo
	frame = NULL;
	width = 0;
	height = 0;
}

/***********************
* Logo
*	Destructor
************************/
Logo::~Logo()
{
	//If we have open a gile
	if(frame)
		//Free it
		free(frame);
}

Logo & Logo::operator =(const Logo& l)
{
	if (frame)
	{
		//Free it
		free(frame);
		frame = NULL;
	}

    width = l.width;
    height= l.height;
	//Get size with padding
	DWORD size = (((width/32+1)*32)*((height/32+1)*32)*3)/2;

	//Allocate frame
	if (frame == NULL) frame = (BYTE*)malloc(size); /* size for YUV 420 */

	memcpy(frame,l.frame, size);

	return *this;
}

int Logo::Load(const char* fileName, unsigned int pwidth, unsigned int pheight)
{
	AVFormatContext *fctx = NULL;
	AVCodecContext *ctx = NULL;
	const AVCodec *codec = NULL;
	AVCodecParameters *par = NULL;
	AVFrame *logoRGB = NULL;
	AVFrame* logo = NULL;
	SwsContext *sws = NULL;
	AVPacket *packet = NULL;
	int res = 0;
	int numpixels = 0;
	int size = 0;

	//Create context from file
	if(avformat_open_input(&fctx, fileName, NULL, NULL)<0)
		return Error("Couldn't open the logo image file [%s]\n",fileName);

	//Check it's ok
	if(avformat_find_stream_info(fctx,NULL)<0)
	{
		//Set error
		res = Error("Couldn't find stream information for the logo image file...\n");
		//Free resources
		goto end;
	}

	//Get stream parameters (AVStream::codec a été supprimé en ffmpeg >= 5)
	par = fctx->streams[0]->codecpar;

	//Get decoder for format
	if (!(codec = avcodec_find_decoder(par->codec_id)))
	{
		//Set errror
		res = Error("Couldn't find codec for the logo image file...\n");
		//Free resources
		goto end;
	}

	//Allocate a decoding context and fill it from the stream parameters
	if (!(ctx = avcodec_alloc_context3(codec)))
	{
		//Set errror
		res = Error("Couldn't alloc codec context\n");
		//Free resources
		goto end;
	}
	if (avcodec_parameters_to_context(ctx, par)<0)
	{
		//Set errror
		res = Error("Couldn't copy codec parameters to context\n");
		//Free resources
		goto end;
	}

	//Open codec
	if (avcodec_open2(ctx, codec, NULL)<0)
	{
		//Set errror
		res = Error("Couldn't open codec for the logo image file...\n");
		//Free resources
		goto end;
	}

	//Alloc packet
	if (!(packet = av_packet_alloc()))
	{
		//Set errror
		res = Error("Couldn't alloc packet\n");
		//Free resources
		goto end;
	}

	//Read logo frame
	if (av_read_frame(fctx, packet)<0)
	{
		//Set errror
		res = Error("Couldn't read frame from the image file...\n");
		//Free resources
		goto end;
	}

	//Alloc frame
	if (!(logoRGB = av_frame_alloc()))
	{
		//Set errror
		res = Error("Couldn't alloc frame\n");
		//Free resources
		goto end;
	}


	//Decode logo: envoie le paquet puis récupère la frame décodée
	if (avcodec_send_packet(ctx, packet)<0)
	{
		//Set errror
		res = Error("Couldn't send packet to decoder\n");
		//Free resources
		goto end;
	}

	if (avcodec_receive_frame(ctx, logoRGB)<0)
	{
		//Set errror
		res = Error("Couldn't decode logo\n");
		//Free resources
		goto end;
	}

	//Allocate new one
	if (!(logo = av_frame_alloc()))
	{
		//Set errror
		res = Error("Couldn't alloc frame\n");
		//Free resources
		goto end;
	}

	//Get frame sizes
	if ( pwidth > 0)
	{
		width = pwidth;
		if (pheight > 0)
		{
			height = pheight;
		}
		else
		{
			// Compute height to keep aspect ratio
			height = (ctx->height * pwidth) / ctx->width;
		}
	}
	else
	{
		if (  pheight > 0 )
		{
			height = pheight;
			width = (ctx->width * pheight) / ctx->height;
		}
		else
		{
			width = ctx->width;
			height = ctx->height;
		}
	}

	// Create YUV rescaller cotext
	if (!(sws = sws_alloc_context()))
	{
		//Set errror
		res = Error("Couldn't alloc sws context\n");
		// Exit
		goto end;
	}

	// Set property's of YUV rescaller context
	av_opt_set_defaults(sws);
	av_opt_set_int(sws, "srcw",       ctx->width		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "srch",       ctx->height		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "src_format", ctx->pix_fmt		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dstw",       width			,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dsth",       height		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dst_format", AV_PIX_FMT_YUV420P	,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "sws_flags",  SWS_FAST_BILINEAR	,AV_OPT_SEARCH_CHILDREN);

	// Init YUV rescaller context
	if (sws_init_context(sws, NULL, NULL) < 0)
	{
		//Set errror
		res = Error("Couldn't init sws context\n");
		// Exit
		goto end;
	}

	//Check if we already had one
	if (frame)
		//Free memory
		free(frame);

	//Get size with padding
	size = GetSize();

	//And numer of pixels
	numpixels = width*height;

	//Allocate frame
	frame = (BYTE*)malloc(size); /* size for YUV 420 */

	//Alloc data
	logo->data[0] = frame;
	logo->data[1] = logo->data[0] + numpixels;
	logo->data[2] = logo->data[1] + numpixels / 4;

	//Set size for planes
	logo->linesize[0] = width;
	logo->linesize[1] = width/2;
	logo->linesize[2] = width/2;

	//Convert
	sws_scale(sws, logoRGB->data, logoRGB->linesize, 0, height, logo->data, logo->linesize);

	//Everything was ok
	res = 1;

end:
	if (packet)
		av_packet_free(&packet);

	if (logo)
		av_frame_free(&logo);

	if (logoRGB)
		av_frame_free(&logoRGB);

	if (ctx)
		avcodec_free_context(&ctx);

	if (sws)
		sws_freeContext(sws);

	if (fctx)
		avformat_close_input(&fctx);

	//Exit
	return res;
}

int Logo::GetWidth()
{
	return width;
}

int Logo::GetHeight()
{
	return height;
}

BYTE* Logo::GetFrame()
{
	return frame;
}

void Logo::Clean()
{
	if ( width == 0 || height == 0 )
	{
		if (frame != NULL)
		{
			free(frame);
			frame = NULL;
		}
		return;
	}
	else
	{
		//Get size with padding
		//DWORD size = (((width/32+1)*32)*((height/32+1)*32)*3)/2;
		DWORD size =  GetSize();
		//Allocate frame
		if (frame == NULL) frame = (BYTE*)malloc(size); /* size for YUV 420 */
		memset(frame, 0, size);
	}
}

void Logo::PaintBlackRectangle(unsigned int pwidth, unsigned int pheight)
{
	if ( pwidth == 0 || pheight == 0)
	{
		return;
	}

	if (frame != NULL)
	{
		free(frame);
		frame = NULL;
	}

	width = pwidth;
	height = pheight;
	unsigned int size =  GetSize();
	unsigned int numPixels = pwidth*pheight;
	frame = (BYTE*)malloc(size); /* size for YUV 420 */

	if ( frame )
	{
	    BYTE *lineaY = frame;
	    BYTE *lineaU = frame + numPixels;
	    BYTE *lineaV = lineaU + numPixels/4;

	    memset(lineaY, 0, numPixels);
	    memset(lineaU, (BYTE) -128, numPixels/4);
	    memset(lineaV, (BYTE) -128, numPixels/4);
	}
}
