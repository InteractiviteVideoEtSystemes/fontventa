#include <string.h>
#include <netinet/in.h>
#include "medkit/log.h"
#include "medkit/video.h"
#include "ffvideocodec.h"


// av_err2str() alloue un tableau temporaire et en prend l'adresse : invalide en C++.
static const char* AVErrToStr(int err)
{
	static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

static void TryVAAPI(AVCodecContext * ctx, const AVCodec *codec)
{
	// Try to enable hardware accelation
	// Check if VAAPI is available for this codec.
	bool use_vaapi = false;

    const AVCodecHWConfig *hw_config = nullptr;
    for (int i = 0; ; i++) {
        hw_config = avcodec_get_hw_config(codec, i);
        if (!hw_config) break;
        if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            hw_config->device_type == AV_HWDEVICE_TYPE_VAAPI) {
			use_vaapi = true;
            break;
        }
    }

	if (use_vaapi)
	{
		AVBufferRef *hw_device_ctx = nullptr;

		Log("FFMpeg encoder [%s] supports VAAPI hardware acceleration\n", codec->name);

		int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
		if (ret < 0)
		{
           	Log("Failed to create VAAPI device. Error: %s. Falling back to CPU.\n", AVErrToStr(ret));
        	ctx->hw_device_ctx = nullptr;
        }
		else
		{
			ctx->hw_device_ctx = hw_device_ctx;
		}
	}
}

// Callback get_format : impose le format matériel VAAPI au décodeur quand il est
// proposé, sinon celui-ci reste en logiciel sans jamais activer le hwaccel.
static enum AVPixelFormat GetVAAPIFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
		if (*p == AV_PIX_FMT_VAAPI)
			return *p;

	Error("Failed to get VAAPI pixel format, falling back to default\n");
	return avcodec_default_get_format(ctx, pix_fmts);
}

static void AllocateVAAPIFrame(AVCodecContext * ctx)
{
	if (ctx->hw_device_ctx) 
	{
		ctx->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
		if (!ctx->hw_frames_ctx)
		{
			Log("Failed to allocate VAAPI frame context. Falling back to CPU.\n");
			av_buffer_unref(&ctx->hw_device_ctx);
			ctx->hw_device_ctx = nullptr;
		}
		else
		{
			AVHWFramesContext *frames_ctx = (AVHWFramesContext *)ctx->hw_frames_ctx->data;

            frames_ctx->format = AV_PIX_FMT_VAAPI;
            frames_ctx->sw_format = AV_PIX_FMT_YUV420P;
            frames_ctx->width = ctx->width;
            frames_ctx->height = ctx->height;
            frames_ctx->initial_pool_size = 20;

            int ret = av_hwframe_ctx_init(ctx->hw_frames_ctx);
            if (ret < 0) {
                Log("Failed to initialize VAAPI frame context: %s. Falling back to CPU.\n", AVErrToStr(ret));
                av_buffer_unref(&ctx->hw_frames_ctx);
				av_buffer_unref(&ctx->hw_device_ctx);
				ctx->hw_device_ctx= nullptr;
            }
		}
	}
}
/***********************
* FfVideoEncoder
************************/
FfVideoEncoder::FfVideoEncoder(const Properties& properties, enum AVCodecID av_codec, enum VideoCodec::Type codec_id)
{
	// Set default values
	frame	= NULL;
	ctx	= NULL;
	picture	= NULL;
	opened	= false;
	type    = codec_id;
	format  = 0;

	// Init framerate
	SetFrameRate(5,300,20);

	// Get encoder
	codec = avcodec_find_encoder(av_codec);

	// Check codec
	if(!codec)
	{
		Error("Encoder [%s] not supported in ffmpeg\n", avcodec_get_name(av_codec));
		return;
	}

	if (codec->type != AVMEDIA_TYPE_VIDEO)
	{
		Error("FFMpeg encoder [%s] is not a video encoder\n", codec->name);
		return;
	}

	//Alocamos el conto y el picture
	ctx = avcodec_alloc_context3(codec);
	picture = av_frame_alloc();
	hw_frame = nullptr;

	TryVAAPI(ctx, codec);
}

/***********************
* ~FfVideoEncoder
*	Destructor
************************/
FfVideoEncoder::~FfVideoEncoder()
{
	if (frame)
		delete frame;

	if (hw_frame)
		av_frame_free(&hw_frame);

	if (ctx)
		avcodec_free_context(&ctx);

	if (picture)
		av_frame_free(&picture);
}

/***********************
* SetSize
*	Inicializa el tamao de la imagen a codificar
************************/
int FfVideoEncoder::SetSize(int width, int height)
{
	Log("-SetSize [%d,%d]\n",width,height);

	// Set pixel format
	if (ctx->hw_device_ctx)
	{
		ctx->pix_fmt = AV_PIX_FMT_VAAPI;
		picture->format = AV_PIX_FMT_VAAPI;
	}
	else
	{
		ctx->pix_fmt = AV_PIX_FMT_YUV420P;
		picture->format		= AV_PIX_FMT_YUV420P;
	}
	
	ctx->width 		= width;
	ctx->height 	= height;

	// L'API avcodec_send_frame() exige que l'AVFrame porte lui-même son
	// format et ses dimensions (l'ancienne API les déduisait du contexte).
	
	picture->width		= width;
	picture->height		= height;

	// Set picture data
	picture->linesize[0] = width;
	picture->linesize[1] = width/2;
	picture->linesize[2] = width/2;

	// Open codec
	return OpenCodec();
}

/*************************
* SetFrameRate
* 	Indica el numero de frames por segudno actual
***************************/
int FfVideoEncoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Save frame rate
	if (frames>0)
		fps=frames;
	else
		fps=10;

	// Save bitrate
	if (kbits>0)
		bitrate=kbits*1024;

	//Save intra period
	if (intraPeriod>0)
		this->intraPeriod = intraPeriod;

	return 1;
}

/***********************
* OpenCodec
*	Abre el codec
************************/
int FfVideoEncoder::OpenCodec()
{
	Log("-OpenCodec %s [%dbps,%dfps]\n", VideoCodec::GetNameFor(type), bitrate, fps);

	// Check
	if (codec==NULL)
		return Error("No codec\n");

	// Check
	if (opened)
		return Error("Already opened\n");

	//If already got a buffer
	if (frame)
		//Free it
		delete(frame);

	//Set new buffer size
	DWORD bufSize = 1.5*bitrate/fps;

	//Check size
	if (bufSize<AV_INPUT_BUFFER_MIN_SIZE)
		//Set minimun
		bufSize = AV_INPUT_BUFFER_MIN_SIZE;

	//Y alocamos el buffer
	frame = new VideoFrame(type,bufSize);

	// Bitrate,fps
	ctx->bit_rate 		= bitrate;
	ctx->bit_rate_tolerance = bitrate/fps+1;
	ctx->time_base          = (AVRational){1,fps};
	ctx->gop_size		= intraPeriod;

	// Encoder quality
	ctx->rc_max_rate	= bitrate;
	ctx->rc_buffer_size	= bitrate/fps+1;
	ctx->rc_initial_buffer_occupancy = 0;
	ctx->max_b_frames	= 0;
	// Tell the encoder to use fixed quantizer (qscale)
	ctx->flags |= AV_CODEC_FLAG_QSCALE;
	// Set the global quality based on a quantizer value of 10
	ctx->global_quality = FF_QP2LAMBDA * 10;

	if (ctx->hw_device_ctx)
	{
		AllocateVAAPIFrame(ctx);

		if (ctx->hw_device_ctx)
		{
			hw_frame = av_frame_alloc();
			if (!hw_frame) {
				Error("Failed to allocate VAAPI frame. Falling back to CPU.\n");
				av_buffer_unref(&ctx->hw_frames_ctx);
				av_buffer_unref(&ctx->hw_device_ctx);
			}
			else
			{
				hw_frame->format = AV_PIX_FMT_VAAPI;
				hw_frame->width = ctx->width;
				hw_frame->height = ctx->height;
			}
		}

		// L'init VAAPI (AllocateVAAPIFrame ou l'allocation de hw_frame) a échoué :
		// SetSize() avait déjà positionné le format matériel, il faut revenir au logiciel.
		if (!ctx->hw_device_ctx)
		{
			ctx->pix_fmt = AV_PIX_FMT_YUV420P;
			picture->format = AV_PIX_FMT_YUV420P;
		}
	}

	// Open codec
	if (avcodec_open2(ctx, codec, NULL)<0)
		return Error("ffmpeg is unable to open %s encoder\n", codec->name);

	// We are opened
	opened=true;

	// Exit
	return 1;
}


/***********************
* EncodeFrame
*	Codifica un frame
************************/
VideoFrame* FfVideoEncoder::EncodeFrame(BYTE *in,DWORD len)
{
	int ret = 0;
	//Check if we are opened
	if (!opened)
		return NULL;

	AVPacket* pkt = av_packet_alloc();

	int numPixels = ctx->width*ctx->height;

	//Comprobamos el tamano
	if (numPixels*3/2 != len)
		return NULL;

	//POnemos los valores
	picture->data[0] = in;
	picture->data[1] = in+numPixels;
	picture->data[2] = in+numPixels*5/4;

	bool firstPacket = true;

	if (hw_frame && ctx->hw_device_ctx && ctx->hw_frames_ctx)
	{
		// HW encoding : on prend une nouvelle surface VAAPI du pool à chaque trame
		// (hw_frame ne porte lui-même aucun buffer tant qu'on ne l'a pas demandé).
		av_frame_unref(hw_frame);
		hw_frame->format = AV_PIX_FMT_VAAPI;
		hw_frame->width  = ctx->width;
		hw_frame->height = ctx->height;

		ret = av_hwframe_get_buffer(ctx->hw_frames_ctx, hw_frame, 0);
		if (ret < 0) {
			Error("Failed to get VAAPI surface: %s\n", AVErrToStr(ret));
			return NULL;
		}

		ret = av_hwframe_transfer_data(hw_frame, picture, 0);
		if (ret < 0) {
			Error("Failed to transfer frame to VAAPI: %s\n", AVErrToStr(ret));
			return NULL;
		}

		ret = avcodec_send_frame(ctx, hw_frame);
	}
	else
	{
		// SW encoding
		ret = avcodec_send_frame(ctx, picture);
	}

	if (ret < 0) {
		Error("Encoding error: %s\n", AVErrToStr(ret));
		return NULL;
	}

	DWORD size = 0;

	do {
		ret = avcodec_receive_packet(ctx, pkt);
		if (ret >= 0)
		{
			if (firstPacket) {
				//Set width and height
				frame->SetWidth(ctx->width);
				frame->SetHeight(ctx->height);

				//Is intra
				frame->SetIntra( (pkt->flags & AV_PKT_FLAG_KEY) != 0 );

				//Unset fpu
				picture->key_frame = 0;
				picture->pict_type = AV_PICTURE_TYPE_NONE;

				//Clean all previous packets
				frame->ClearRTPPacketizationInfo();

				firstPacket = false;
			}

			// Recopie la trame binaire encodée dans le tampon de la VideoFrame.
			// Avec l'API send/receive, ffmpeg écrit dans SON propre tampon
			// (pkt->data) ; la packetisation RTP (PacketizeFrame) référence des
			// offsets dans le tampon de la frame, qu'il faut donc remplir.
			frame->SetMedia(pkt->data, pkt->size);
			size += pkt->size;
		}
		else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			// Plus de paquet disponible pour cette trame : fin normale du drain.
			// (NE PAS retourner NULL ici : la trame déjà packetisée serait perdue.)
			break;
		}
		else
		{
			Error("Encoding error (receive_packet): %s\n", AVErrToStr(ret));
			av_packet_free(&pkt);
			return NULL;
		}
		av_packet_unref(pkt);
	} while(ret >= 0);

	av_packet_free(&pkt);

	// Aucune donnée produite (trame éventuellement bufferisée par l'encodeur).
	if (size == 0)
		return NULL;

	//Set length
	frame->SetLength(size);

	// Packetisation RTP propre au codec (virtuelle).
	PacketizeFrame();

	return frame;
}

/***********************
* PacketizeFrame (défaut)
*	Schéma historique H263/MPEG4 : saute le start code (2 octets) et fragmente
*	avec un préfixe de payload RFC 2429. Redéfini par les codecs à packetisation
*	RTP spécifique (cf. VP8Encoder::PacketizeFrame).
************************/
void FfVideoEncoder::PacketizeFrame()
{
	DWORD len = frame->GetLength();
	DWORD ini = 2;		// saute le start code 2 octets
	BYTE prefix[2] = { 0x04, 0x00 };

	while (ini < len)
	{
		bool mark = false;
		DWORD lenpkt = RTPPAYLOADSIZE-2;
		if (lenpkt+ini >= len)
		{
			mark = true;
			lenpkt = len-ini;
		}

		frame->AddRtpPacket(ini, lenpkt, prefix, 2, mark);

		// Paquets suivants : préfixe sans le bit P.
		if (ini == 2)
		{
			prefix[0] = 0x00;
			prefix[1] = 0x00;
		}

		ini += lenpkt;
	}
}

/***********************
* FastPictureUpdate
*	Manda un frame entero
************************/
int FfVideoEncoder::FastPictureUpdate()
{
	//If we have picture
	if (picture)
	{
		//Set it
		picture->key_frame = 1;
		picture->pict_type = AV_PICTURE_TYPE_I;
	}

	if (ctx->hw_device_ctx && hw_frame) 
	{
		hw_frame->key_frame = 1;
		hw_frame->pict_type = AV_PICTURE_TYPE_I;
	}

	return 1;
}

/***********************
* FfVideoDecoder
*	Consturctor
************************/
FfVideoDecoder::FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id)
{
	//Guardamos los valores por defecto
	type = codec_id;
	bufLen = 0;

	//Encotramos el codec
	codec = avcodec_find_decoder(av_codec);

	//Comprobamos
	if(codec==NULL)
	{
		Error("No decoder found\n");
		return ;
	}

	//Alocamos el contxto y el picture
	ctx = avcodec_alloc_context3(codec);
	parser_ctx = av_parser_init(ctx->codec_id);

	if (!parser_ctx) {
        Error("Unable to open FFmpeg decoder (parser). Codec ID = %s", VideoCodec::GetNameFor(type));
    }
	picture = av_frame_alloc();

	TryVAAPI(ctx, codec);
	if (ctx->hw_device_ctx)
		// Sans ce callback le décodeur ne négocie jamais le format matériel
		// et reste en logiciel malgré hw_device_ctx.
		ctx->get_format = GetVAAPIFormat;

	//POnemos los valores del contexto
	ctx->workaround_bugs 	= 255*255;
	ctx->error_concealment 	= FF_EC_GUESS_MVS | FF_EC_DEBLOCK;

	//Alocamos el buffer
	bufSize = 1024*756*3/2;
	buffer = (BYTE *)malloc(bufSize);
	frame = NULL;
	frameSize = 0;
	src = 0;

	//Lo abrimos
	avcodec_open2(ctx, codec, NULL);
}

/***********************
* ~FfVideoDecoder
*	Destructor
************************/
FfVideoDecoder::~FfVideoDecoder()
{
	free(buffer);
	if (frame!=NULL)
		free(frame);
	if (parser_ctx)
		av_parser_close(parser_ctx);
	av_frame_free(&picture);
	avcodec_free_context(&ctx);	// ferme aussi le codec
}

int FfVideoDecoder::Decode(BYTE *buffer,DWORD size)
{
	//Decodificamos
	AVPacket* pkt = av_packet_alloc();
	BYTE *current_ptr = buffer;
    int remaining_size = size;
	int ret;

	while (remaining_size > 0) {
		if (parser_ctx) {
			int bytes_parsed = av_parser_parse2(
	            parser_ctx, ctx,
	            &pkt->data, &pkt->size,
	            current_ptr, remaining_size,
	            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0
	        );

			if (bytes_parsed < 0) {
	            Error("Error from %s parser. Error = %d\n", VideoCodec::GetNameFor(type), bytes_parsed);
	            goto error;
	        }

			current_ptr += bytes_parsed;
	        remaining_size -= bytes_parsed;
		} else {
			// Pas de parser pour ce codec (ex. H263+) : l'entrée est déjà une
			// trame complète, on la pousse telle quelle.
			pkt->data = current_ptr;
			pkt->size = remaining_size;
			remaining_size = 0;
		}

		if (pkt->size > 0) {
			ret = avcodec_send_packet(ctx, pkt);

			if (ret < 0) {
				Error("%s decoding error (send packet). Error = %s\n", VideoCodec::GetNameFor(type), AVErrToStr(ret));
                goto error;
            }

			// avcodec_receive_frame() fait toujours un av_frame_unref(picture) en
			// entrée (doc ffmpeg) : un 2e appel après un succès écraserait aussitôt
			// la frame qu'on vient de recevoir, avant même que Decode() ne rende la
			// main à l'appelant pour que GetFrame() la lise. On s'arrête donc dès la
			// première frame obtenue ; 'picture' garde alors la dernière frame
			// décodée avec succès tant qu'aucune nouvelle frame n'est disponible.
			ret = avcodec_receive_frame(ctx, picture);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				// Pas de frame prête pour ce paquet (ex: NAL SPS/PPS seul) : normal.
			} else if (ret < 0) {
				Error("%s decoding error (receive frame). Error = %s\n", VideoCodec::GetNameFor(type), AVErrToStr(ret));
				goto error;
			}
		}
	}

	if (pkt) av_packet_free(&pkt);

	return 1;
error:
	if (pkt) av_packet_free(&pkt);
	return 0;
}

/***********************
* DecodePacket (défaut)
*	Dépaquetisation RTP générique : accumule le payload brut dans le tampon
*	membre et décode la trame complète sur 'last'. Convient aux codecs sans
*	en-tête de payload RTP à retirer (MPEG4, VP6, FLV1/SORENSON...).
*	Les codecs à dépaquetisation spécifique (H264, H263+, VP8) redéfinissent
*	cette méthode dans leur propre classe dérivée.
************************/
int FfVideoDecoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	// Vérifie la place disponible (+ padding ffmpeg)
	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE > bufSize)
	{
		Log("-DecodePacket buffer size error, reseting\n");
		bufLen = 0;
		return 0;
	}

	// Payload déjà exploitable tel quel : accumulation brute.
	memcpy(buffer+bufLen,in,inLen);
	bufLen += inLen;

	// Dernier paquet de la trame : on décode.
	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		ret = Decode(buffer,bufLen);
		bufLen = 0;
	}

	return ret;
}

BYTE* FfVideoDecoder::GetFrame()
{
	if (!picture)
		return NULL;

	if (picture->width==0 || picture->height==0) {
		Error("-Wrong dimensions [%d,%d]\n",picture->width,picture->height);
		return NULL;
	}

	AVFrame *output_frame = picture;
	AVFrame *sw_frame = nullptr;

	if (picture->format == AV_PIX_FMT_VAAPI)
	{
		// This was a frame that was decoded using VAAPI
		sw_frame = av_frame_alloc();
		sw_frame->format = AV_PIX_FMT_YUV420P;
		sw_frame->width = picture->width;
		sw_frame->height = picture->height;

		int ret = av_frame_get_buffer(sw_frame, 0);
		if (ret < 0)
		{
			Error("Failed to allocate software frame: %s\n", AVErrToStr(ret));
			av_frame_free(&sw_frame);
			return NULL;
		}

		ret = av_hwframe_transfer_data(sw_frame, picture, 0);
		if (ret < 0)
		{
			Error("Failed to transfer data from hardware frame: %s\n", AVErrToStr(ret));
			av_frame_free(&sw_frame);
			return nullptr;
		}
		output_frame = sw_frame;
	}

	int w = output_frame->width;
	int h = output_frame->height;
	int u = w*h;
	int v = w*h*5/4;
	int size = w*h*3/2;

	//Comprobamos el tamano
	if (size>frameSize)
	{
		Log("-Frame size %dx%d\n",w,h);
		if (frame!=NULL)
		{
			frame = (BYTE*) realloc(frame, size);
		}
		else
		{
			frame = (BYTE*) malloc(size);
		}
		frameSize = size;
	}

	//Copaamos  el Cy
	for(int i=0;i<ctx->height;i++)
		memcpy(&frame[i*w],(void*) &output_frame->data[0][i*output_frame->linesize[0]],w);

	//Y el Cr y Cb
	for(int i=0;i<ctx->height/2;i++)
	{
		memcpy(&frame[i*w/2+u],(void*) &output_frame->data[1][i*output_frame->linesize[1]],w/2);
		memcpy(&frame[i*w/2+v],(void*) &output_frame->data[2][i*output_frame->linesize[2]],w/2);
	}

	// Frame système intermédiaire : copiée dans `frame`, on n'en a plus besoin.
	if (sw_frame)
		av_frame_free(&sw_frame);

	return frame;
}
