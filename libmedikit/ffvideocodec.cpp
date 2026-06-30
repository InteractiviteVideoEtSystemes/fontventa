#include <string.h>
#include <netinet/in.h>
#include "medkit/log.h"
#include "medkit/video.h"
#include "ffvideocodec.h"

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
}

/***********************
* ~FfVideoEncoder
*	Destructor
************************/
FfVideoEncoder::~FfVideoEncoder()
{
	if (frame)
		delete frame;

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
	ctx->pix_fmt		= AV_PIX_FMT_YUV420P;
	ctx->width 		= width;
	ctx->height 		= height;

	// L'API avcodec_send_frame() exige que l'AVFrame porte lui-même son
	// format et ses dimensions (l'ancienne API les déduisait du contexte).
	picture->format		= AV_PIX_FMT_YUV420P;
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

	//Codificamos
	int ret = avcodec_send_frame(ctx, picture);

	if (ret < 0) {
		Error("Encoding error: %d\n", ret);
		return NULL;
	}

	//Skip first two
	DWORD ini = 2;
	DWORD size = 0;
	BYTE prefix[2];

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

				//Set header for first
				prefix[0] = 0x04;
				prefix[1] = 0x00;

				//Clean all previous packets
				frame->ClearRTPPacketizationInfo();
			}

			// Recopie la trame binaire encodée dans le tampon de la VideoFrame.
			// Avec l'API send/receive, ffmpeg écrit dans SON propre tampon
			// (pkt->data) ; la packetisation RTP ci-dessous référence des
			// offsets dans le tampon de la frame, qu'il faut donc remplir.
			frame->SetMedia(pkt->data, pkt->size);

			//Copy all
			DWORD lenpkt;
			bool mark ;

			while(ini<pkt->size)
			{
				mark = false;
				//The mtu
				lenpkt = RTPPAYLOADSIZE-2;
				//Check length
				if (lenpkt+ini >= pkt->size)
				{
					mark = true;
					//Fix it
					lenpkt=pkt->size-ini;
				}

				//Add rtp packet
				frame->AddRtpPacket(ini,lenpkt,prefix,2, mark );

				//If it is first
				if (ini==2)
				{
					//Set header for the nexts
					prefix[0] = 0x00;
					prefix[1] = 0x00;
				}

				//Increase pointer
				ini += lenpkt;
			}

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
			Error("Encoding error (receive_packet): %d\n", ret);
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
	return frame;
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
				// TODO: use av_err2str()
                Error("%s decoding error (send packet). Error = %d\n", VideoCodec::GetNameFor(type), ret);
                goto error;
            }

			while (ret >= 0) {
                ret = avcodec_receive_frame(ctx, picture);
                // EAGAIN : besoin de plus de données ; EOF : flux terminé.
                // Ce sont des fins de drain normales, pas des erreurs.
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    Error("%s decoding error (receive frame). Error = %d\n", VideoCodec::GetNameFor(type), ret);
                	goto error;
                }

				if (picture->width==0 || picture->height==0) {
					Error("-Wrong dimmensions [%d,%d]\n",picture->width,picture->height);
					goto error;
				}

				int w = picture->width;
				int h = picture->height;
				int u = w*h;
				int v = w*h*5/4;
				int size = w*h*3/2;

				//Comprobamos el tamano
				if (size>frameSize)
				{
					Log("-Frame size %dx%d\n",w,h);
					//Liberamos si habia
					if(frame!=NULL)
						free(frame);
					//Y allocamos de nuevo
					frame = (BYTE*) malloc(size);
					frameSize = size;
				}

				//Copaamos  el Cy
				for(int i=0;i<ctx->height;i++)
					memcpy(&frame[i*w],(void*) &picture->data[0][i*picture->linesize[0]],w);

				//Y el Cr y Cb
				for(int i=0;i<ctx->height/2;i++)
				{
					memcpy(&frame[i*w/2+u],(void*) &picture->data[1][i*picture->linesize[1]],w/2);
					memcpy(&frame[i*w/2+v],(void*) &picture->data[2][i*picture->linesize[2]],w/2);
				}
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
