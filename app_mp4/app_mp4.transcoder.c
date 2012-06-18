/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Sergio Garcia Murillo <sergio.garcia@fontventa.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief MP4 application -- save and play mp4 files
 *
 * \ingroup applications
 */

#include <asterisk.h>
#include <mp4v2/mp4v2.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/utils.h>
#include <asterisk/app.h>
#include <asterisk/version.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>



#ifndef AST_FORMAT_AMR
#define AST_FORMAT_AMR		(1 << 13)
#define AST_FORMAT_MPEG4 	(1 << 22)
#endif

#define PKT_PAYLOAD     1450
#define PKT_SIZE        (sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + PKT_PAYLOAD)
#define PKT_OFFSET      (sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET)
#define FRAME_SIZE      65535 
#define NO_CODEC        -1

#if ASTERISK_VERSION_NUM>999999   // 10600
#define AST_FRAME_GET_BUFFER(fr)        ((uint8_t *)((fr)->data.ptr))
#else
#define AST_FRAME_GET_BUFFER(fr)        ((uint8_t *)((fr)->data))
#endif


/*! \brief codec video dans le fichier 
 */
typedef enum 
{
  NATIVE_VIDEO_CODEC_H264 = 0 ,
  NATIVE_VIDEO_CODEC_H263P ,
  NATIVE_VIDEO_CODEC_H263 ,
  NATIVE_VIDEO_CODEC_LAST // Always last 
} NativeCodec;

struct VideoTranscoder
{
    int end;
    struct ast_channel* channel;
    AVFrame*            picture;
    uint8_t*            bufPicture;
    /* Decoder */
    AVCodec*         decoder;
    AVCodecContext*  decoderCtx;
    int		           decoderOpened;
    uint8_t          ebit;
 
    int 	newPic;

    uint8_t		*frame;
    uint32_t	frameSize;
    uint32_t	frameLen;


    /* Encoder */
    AVCodec         *encoder;
    AVCodecContext  *encoderCtx;

    int		encoderFormat;
    int		encoderOpened;
	
    uint8_t		*buffer;
    uint32_t	bufferSize;
    uint32_t	bufferLen;
    int 	mb;
    int	mb_total;
    int 	sent_bytes;

    /* Encoder Params */
    int	bitrate;
    int	fps;
    int	format;
    int	qMin;
    int	qMax;
    int	encoderWidth;
    int 	encoderHeight;
    int 	gop_size;
    int frame_count ;

    /* Encoder thread */
    pthread_t encoderThread;

    /* Resize */
    struct SwsContext* resizeCtx;
    int	resizeWidth;
    int	resizeHeight;
    AVFrame*         resizePicture; 
    int	resizeSrc[3];
    int	resizeDst[3];
    int	resizeFlags;
};

struct RFC2190H263HeadersBasic
{
        //F=0             F=1
        //P=0   I/P frame       I/P mode b
        //P=1   B frame         B fame mode C
        uint32_t trb:9;
        uint32_t tr:3;
        uint32_t dbq:2;
        uint32_t r:3;
        uint32_t a:1;
        uint32_t s:1;
        uint32_t u:1;
        uint32_t i:1;
        uint32_t src:3;
        uint32_t ebits:3;
        uint32_t sbits:3;
        uint32_t p:1;
        uint32_t f:1;
};

static const char *app_play = "mp4play";
static const char *syn_play = "MP4 file playblack";
static const char *des_play = "  mp4play(filename,[options]):  Play mp4 file to user. \n"
        "\n"
        "Available options:\n"
        " 'n(x)': number of digits (x) to wait for \n"
        " 'S(x)': set variable of name x (with DTMFs) rather than go to extension \n"
        " 's(x)': set digits, which should stop playback \n"
        "\n"
        "Examples:\n"
        " mp4play(/tmp/video.mp4)   					play video file to user\n"
        " mp4play(/tmp/test.mp4,'n(3)') 				play video file to user and wait for 3 digits \n"
        " mp4play(/tmp/test.mp4,'n(3)S(DTMF_INPUT)')	play video file to user and wait for 3 digits and \n"
        "							set them as value of variable DTMF_INPUT\n"
        " mp4play(/tmp/test.mp4,'n(3)s(#)') 		play video file, wait for 3 digits or break on '#' \n";


static const char *app_save = "mp4save";
static const char *syn_save = "MP4 file record";
static const char *des_save = "  mp4save(filename,[options]):  Record mp4 file. \n"
        "Note: If you are working with amr it's recommended that you use 3gp\n"
        "as your file extension if you want to play it with a video player.\n"
        "\n"
        "Available options:\n"
        " 'v': activate loopback of video\n"
        " 'V': wait for first video I frame to start recording\n"
	" '0'..'9','#','*': sets dtmf input to stop recording"
        "\n"
        "Note: waiting for video I frame also activate video loopback mode.\n"
        "\n"
        "Examples:\n"
        " mp4save(/tmp/save.3gp)    record video to selected file\n"
        " mp4save(/tmp/save.3gp,#)  record video and stop on '#' dtmf\n"
        " mp4save(/tmp/save.3gp,v)  activate loopback of video\n"
        " mp4save(/tmp/save.3gp,V)  wait for first videoto start recording\n"
        " mp4save(/tmp/save.3gp,V9) wait for first videoto start recording\n"
        "                           and stop on '9' dtmf\n";
typedef struct {
    int32_t       pos;
    int32_t       bufferSize;
    uint8_t *     buffer;
}MediaBuff ;

struct mp4track {
    MP4FileHandle mp4;
    MP4TrackId track;
    MP4TrackId trackH264;
    MP4TrackId hint;
    bool first;
    bool intra;
    uint8_t frame[FRAME_SIZE];
    int length;
    int sampleId;
    struct VideoTranscoder *vtc ;
    MediaBuff mediaBuff ;
    MediaBuff savMediaBuff ;
};



typedef enum {
  OPT_DFTMINTOVAR 	=	(1 << 0),
  OPT_NOOFDTMF 		=	(1 << 1),
	OPT_STOPDTMF		=	(1 << 2),
} Mp4play_exec_option_flags;

Mp4play_exec_option_flags mp4play_exec_option_flags;

typedef enum {
  OPT_ARG_DFTMINTOVAR =          0,
  OPT_ARG_NOOFDTMF,
	OPT_ARG_STOPDTMF,
  /* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
} Mp4play_exec_option_args;

Mp4play_exec_option_args mp4play_exec_option_args;

#if 1
AST_APP_OPTIONS(mp4play_exec_options, {
    AST_APP_OPTION_ARG('S', OPT_DFTMINTOVAR, OPT_ARG_DFTMINTOVAR),
      AST_APP_OPTION_ARG('n', OPT_NOOFDTMF, OPT_ARG_NOOFDTMF),
      AST_APP_OPTION_ARG('s', OPT_STOPDTMF, OPT_ARG_STOPDTMF),
      });
#else
//#define AST_APP_OPTION_ARG(option, flagno, argno)           \
//        [option] = { .flag = flagno, .arg_index = argno + 1 }

static const struct ast_app_option mp4play_exec_options[128]= {
  'S' = { .flag = OPT_DFTMINTOVAR , .arg_index = OPT_ARG_DFTMINTOVAR+1 },
  AST_APP_OPTION_ARG('n', OPT_NOOFDTMF, OPT_ARG_NOOFDTMF),
  AST_APP_OPTION_ARG('s', OPT_STOPDTMF, OPT_ARG_STOPDTMF),
}
#endif
#define MAX_DTMF_BUFFER_SIZE 25

//void dump_buffer_hex(const unsigned char * text, const unsigned char * buff, int len)
//{
//        int i;
//        unsigned char *temp;
//        temp = malloc(len*3+20); /* actually only len*3 would be needed, but negative values need more then 2 bytes*/
//        if (temp == NULL) {
//                ast_log(LOG_DEBUG, "app_mp4.c: dump_buffer_hex: failed to allocate buffer!\n");
//                return;
//        }
//        for (i=0;i<len;i++) {
//                sprintf( temp+3*i, "%02x ", buff[i]);
//        }
//        ast_log(LOG_DEBUG, "app_mp4.c: dump_buffer_hex: %s\n%s\n",text,temp);
//        free(temp);
//}

extern int option_verbose;

// Transcoder
int CloneBuffer(MediaBuff* dest , MediaBuff* src ){
  int status = 0 ; 
  if  ( !src->bufferSize || !src->buffer ) {
    ast_log(LOG_WARNING, "Invalid param\n");
  } 
  else {
    if ( !dest->buffer ){
      dest->buffer = (uint8_t *) malloc (src->bufferSize * sizeof(uint8_t));
      dest->bufferSize = src->bufferSize ;
    }
    else if ( dest->bufferSize < src->bufferSize ) {
      dest->buffer = (uint8_t *) realloc ( dest->buffer , (src->bufferSize * sizeof(uint8_t )));
      dest->bufferSize = src->bufferSize ;
    }
    if ( dest->buffer ) {
      dest->pos = src->bufferSize ;
      memcpy( dest->buffer , src->buffer , src->bufferSize );
      status = 1 ;
    }
    else
    {
      ast_log(LOG_WARNING, "allocation failed \n");
    }
  }
  return status ;
}


void av_log_asterisk_callback(void* ptr, int level, const char* fmt, va_list vl)
{
  if ( option_debug > 2 ){
    char msg[1024];

    vsnprintf(msg,1024,fmt,vl);

    AVClass* avc= ptr ? *(AVClass**)ptr : NULL;
    if(avc)
      ast_log(LOG_DEBUG,"[%s @ %p] %s",avc->item_name(ptr), avc, msg);
    else 
      ast_log(LOG_DEBUG, msg);
  }
}


int VideoTranscoderDestroy(struct VideoTranscoder *vtc)
{
	/* End encoder */
	vtc->end = 1;

	/* Free pictures */
  if ( vtc->picture )
    free(vtc->picture );
 
  if ( vtc->bufPicture  )
    free(vtc->bufPicture );

	/* Free frames */
	free(vtc->frame);
	free(vtc->buffer);

	/* Free decoder */
	if (vtc->decoderCtx)
	{
		/*If already open */
		if (vtc->decoderOpened)
			/* Close */
			avcodec_close(vtc->decoderCtx);
		/* Free */
    free(vtc->decoderCtx);
	}

	/* Free encoder */
	if (vtc->encoderCtx)
	{
		/* If encoder opened */
		if (vtc->encoderOpened)
			/* Close */
			avcodec_close(vtc->encoderCtx);
		free(vtc->encoderCtx);
	}


	/* if got contex */
	if (vtc->resizeCtx)
		/* Free it */
		sws_freeContext(vtc->resizeCtx);


	/* Free resize buffer*/
	if (vtc->resizePicture){
		/* Free */
		 avpicture_free((AVPicture *)vtc->resizePicture);
     av_free(vtc->resizePicture);
  }
	/* Free */
	free(vtc);

	/* Exit */
	return 1;
}

#define H263P_HEADER_SIZE		2
#define H263_HEADER_MODE_A_SIZE 4
#define H263_HEADER_MODE_B_SIZE 8
#define H263_HEADER_MODE_C_SIZE 12
static uint32_t rfc2190_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen)
{
	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"RFC2190 appending [%d:0x%.2x,0x%.2x]\n",bufferLen,buffer[0],buffer[1]);

	/* Check length */
	if (bufferLen<sizeof(struct RFC2190H263HeadersBasic))
		/* exit */
		return 0;

	/* Get headers */
	uint32_t x = ntohl(*(uint32_t *)buffer);

	/* Set headers */
	struct RFC2190H263HeadersBasic *headers = (struct RFC2190H263HeadersBasic *)&x;

	/* Get ini */
	uint8_t* in = buffer + sizeof(struct RFC2190H263HeadersBasic);
	uint32_t  len = sizeof(struct RFC2190H263HeadersBasic);

	if (headers->f)
	{
	  if (headers->p)
      /* If C type */
	  {
      if ( option_debug > 4 )
        ast_log(LOG_DEBUG,"RFC2190 C type %d\n",headers->f);

		  /* Skip rest of header */
		  in+=8;
		  len+=8;
	  }
	  else
      /* If B type */
	  {
      if ( option_debug > 4 )
        ast_log(LOG_DEBUG,"RFC2190 B type %d\n",headers->f);

		  /* Skip rest of header */
		  in+=4;
		  len+=4;
		}
	}
	else
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 A type %d\n",headers->f);
  }

	//ast_log(LOG_DEBUG,"RFC2190 len %d\n",len);

	if(headers->ebits!=0)
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 cut %d ebit (%d)\n", headers->ebits, bufferLen-len);
	  in[bufferLen-len-1] &= 0xff << headers->ebits;
	}	

	/* Mix first and end byte */
	if(headers->sbits!=0 && destLen>0)
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 mix %d sbit\n", headers->sbits);

		/* Append to previous byte */
    in[0] &= 0xff >> headers->sbits;		
		dest[destLen-1] |= in[0];
		/* Skip first */
		in++;
		len++;
	}	

	/* Copy the rest */
	memcpy(dest+destLen,in,bufferLen-len);	

  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"RFC2190 max write %d\n", destLen+bufferLen-len);

	/* Return added */
	return bufferLen-len;
}



uint32_t rfc2429_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen)
{
	/* Debug */
	ast_log(LOG_DEBUG,"RFC2429 appending [%d:0x%.2x,0x%.2x]\n",bufferLen,buffer[0],buffer[1]);

	/* Check length */
	if (bufferLen<2)
		/* exit */
		return 0;

	 /* Get header */
	uint8_t p = buffer[0] & 0x04;
	uint8_t v = buffer[0] & 0x02;
	uint8_t plen = ((buffer[0] & 0x1 ) << 5 ) | (buffer[1] >> 3);
	uint8_t pebit = buffer[0] & 0x7;

	/* Get ini */
	uint8_t* in = buffer+2+plen;
	uint32_t  len = bufferLen-2-plen;

	/* Check */
	if (v)
	{
		/* Increase ini */
		in++;
		len--;
	}

	/* Check p bit */
	if (p)
	{
		/* Decrease ini */
		in -= 2;
		len += 2;
		/* Append 0s */
		buffer[0] = 0;
		buffer[1] = 0;
	}

	/* Copy the rest */
	memcpy(dest+destLen,in,len);

	/* Return added */
	return len;
}

/* 3 zero bytes syncword */
static const uint8_t sync_bytes[] = { 0, 0, 0, 1 };


uint32_t h264_append(uint8_t *dest, uint32_t destLen, uint32_t destSize, uint8_t *buffer, uint32_t bufferLen)
{
	uint8_t nal_unit_type;
	unsigned int header_len;
	uint8_t nal_ref_idc;
	unsigned int nalu_size;

	uint32_t payload_len = bufferLen;
	uint8_t *payload = buffer;
	uint8_t *outdata = dest+destLen;
	uint32_t outsize = 0;


	/* +---------------+
	 * |0|1|2|3|4|5|6|7|
	 * +-+-+-+-+-+-+-+-+
	 * |F|NRI|  Type   |
	 * +---------------+
	 *
	 * F must be 0.
	 */
	nal_ref_idc = (payload[0] & 0x60) >> 5;
	nal_unit_type = payload[0] & 0x1f;

	/* at least one byte header with type */
	header_len = 1;

	ast_log(LOG_DEBUG, "h264 receiving %d bytes nal unit type %d\n", payload_len, nal_unit_type);

	switch (nal_unit_type) 
	{
		case 0:
		case 30:
		case 31:
			/* undefined */
			return 0;
		case 25:
			/* STAP-B		Single-time aggregation packet		 5.7.1 */
			/* 2 byte extra header for DON */
			/** Not supported */
			return 0;	
		case 24:
		{
			/**
			   Figure 7 presents an example of an RTP packet that contains an STAP-
			   A.  The STAP contains two single-time aggregation units, labeled as 1
			   and 2 in the figure.

			       0                   1                   2                   3
			       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                          RTP Header                           |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |STAP-A NAL HDR |         NALU 1 Size           | NALU 1 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 1 Data                           |
			      :                                                               :
			      +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |               | NALU 2 Size                   | NALU 2 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 2 Data                           |
			      :                                                               :
			      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                               :...OPTIONAL RTP padding        |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

			      Figure 7.  An example of an RTP packet including an STAP-A and two
					 single-time aggregation units
			*/
			ast_log(LOG_DEBUG, "STAP-A NAL\n");

			/* Skip STAP-A NAL HDR */
			payload++;
			payload_len--;
			
			/* STAP-A Single-time aggregation packet 5.7.1 */
			while (payload_len > 2) 
			{
				/* Get NALU size */
				nalu_size = (payload[0] << 8) | payload[1];

				ast_log(LOG_DEBUG, "STAP-A NAL of size %d %d\n", nalu_size, payload_len);

				/* strip NALU size */
				payload += 2;
				payload_len -= 2;

				if (nalu_size > payload_len)
					nalu_size = payload_len;

				outsize += nalu_size + sizeof (sync_bytes);

				/* Check size */
				if (outsize + destLen >destSize)
				{	
					ast_log(LOG_DEBUG, "Frame to small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
					return 0;
				}

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata += nalu_size;

				payload += nalu_size;
				payload_len -= nalu_size;
			}

			return outsize;
		}
		case 26:
			/* MTAP16 Multi-time aggregation packet	5.7.2 */
			header_len = 5;
			return 0;
			break;
		case 27:
			/* MTAP24 Multi-time aggregation packet	5.7.2 */
			header_len = 6;
			return 0;
			break;
		case 28:
		case 29:
		{
			/* FU-A	Fragmentation unit	 5.8 */
			/* FU-B	Fragmentation unit	 5.8 */
			uint8_t S, E;

			/* +---------------+
			 * |0|1|2|3|4|5|6|7|
			 * +-+-+-+-+-+-+-+-+
			 * |S|E|R| Type	   |
			 * +---------------+
			 *
			 * R is reserved and always 0
			 */
			S = (payload[1] & 0x80) == 0x80;
			E = (payload[1] & 0x40) == 0x40;

			ast_log(LOG_DEBUG, "S %d, E %d", S, E);

			if (S) 
			{
				/* NAL unit starts here */
				uint8_t nal_header;

				/* reconstruct NAL header */
				nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);

				/* strip type header, keep FU header, we'll reuse it to reconstruct
				 * the NAL header. */
				payload += 1;
				payload_len -= 1;

				nalu_size = payload_len;
				outsize = nalu_size + sizeof (sync_bytes);
				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata[0] = nal_header;
				outdata += nalu_size;
				return outsize;

			} else {
				/* strip off FU indicator and FU header bytes */
				payload += 2;
				payload_len -= 2;

				outsize = payload_len;
				memcpy (outdata, payload, outsize);
				outdata += nalu_size;
				return outsize;
			}

			/* if NAL unit ends, flush the adapter */
			if (E) 
			{
				ast_log(LOG_DEBUG, "output %d bytes", outsize);

				return 0;
			}

			return outsize;
			break;
		}
		default:
		{
			/*rtph264depay->wait_start = FALSE;*/

			/* 1-23	 NAL unit	Single NAL unit packet per H.264	 5.6 */
			/* the entire payload is the output buffer */
			nalu_size = payload_len;
			outsize = nalu_size + sizeof (sync_bytes);
			memcpy (outdata, sync_bytes, sizeof (sync_bytes));
			outdata += sizeof (sync_bytes);
			memcpy (outdata, payload, nalu_size);
			outdata += nalu_size;

			return outsize;
		}
	}

	return 0;
}


uint32_t mpeg4_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen)
{
	/* Just copy */
	memcpy(dest+destLen,buffer,bufferLen);
	/* Return added */
	return bufferLen;
}


void VideoTranscoderCleanFrame(struct VideoTranscoder *vtc)
{
	/* Reset length*/
	vtc->frameLen = (uint32_t)0;
}

void VideoTranscoderSetDecoder(struct VideoTranscoder *vtc,int codec)
{
	/* If already opened that codec */
	if (vtc->decoder && vtc->decoderCtx->codec_id == codec)
		/* Exit */
		return;

	/*If already open */
	if (vtc->decoderOpened)
		/* Close previous one */
		avcodec_close(vtc->decoderCtx);

	/* Get decoder */
	vtc->decoder = avcodec_find_decoder(codec);

	/* Clean frame */
	VideoTranscoderCleanFrame(vtc);

  /* Set context parameters*/
  vtc->decoderCtx->workaround_bugs    = 1;
  vtc->decoderCtx->error_concealment  = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;
	vtc->decoderCtx->flags |= CODEC_FLAG_PART;

  /* Open */
  avcodec_open(vtc->decoderCtx, vtc->decoder);

	/* We are open*/
	vtc->decoderOpened = 1;
}



int VideoTranscoderDecodeFrame(struct VideoTranscoder *vtc)
{
	uint8_t *bufDecode;
	int     got_picture;
	int     i;

  int len = vtc->frameLen ;
  char* buffer = vtc->frame ;
   if ( option_debug > 4 )
     ast_log(LOG_DEBUG,"DecodeFrame len [%d]  [%d][%d][%d]\n",len, buffer[0], buffer[1] ,((buffer[2] & 0xfc) == 0x80));
	/* Decode */
	avcodec_decode_video(vtc->decoderCtx,vtc->picture,&got_picture,vtc->frame,vtc->frameLen);
#if 0
	/* If it can be decoded */
	if (got_picture)
	{
		/* Check size */
		if(vtc->decoderCtx->width==0 || vtc->decoderCtx->height==0)
			/* Exit */
			return 0;

		/* Get pointer to frame */
		bufDecode = vtc->bufPicture;

		/* Save size */
		vtc->width  = vtc->decoderCtx->width;
		vtc->height = vtc->decoderCtx->height;

		/* Get sizes */
		int h = vtc->decoderCtx->height;
		int w = vtc->decoderCtx->width;
		int t = vtc->decoderCtx->width/2;
		int u = w*h;
		int v = w*h*5/4;

		/* Copy Y */
		for(i=0;i<h;i++)
			memcpy(&bufDecode[i*w],&vtc->picture->data[0][i*vtc->picture->linesize[0]],w);

		/* Copy U & V */
		for(i=0;i<h/2;i++)
		{
			memcpy(&bufDecode[i*t+u],&vtc->picture->data[1][i*vtc->picture->linesize[1]],t);
			memcpy(&bufDecode[i*t+v],&vtc->picture->data[2][i*vtc->picture->linesize[2]],t);
		}

		/* Set new frame flag */
		vtc->newPic = 1;
	}
#endif
	/* Got frame */
	return got_picture;
}


uint32_t VideoTranscoderWrite(struct VideoTranscoder *vtc, int codec, uint8_t *buffer, uint32_t bufferLen, int mark)
{
	/* Debug */
	//ast_log(LOG_DEBUG,"Received video [%x,%d,%d]\n",codec,bufferLen,mark);

	/* If not enougth */
	if (bufferLen + vtc->frameLen > vtc->frameSize){
    ast_log(LOG_WARNING,"Buffer full\n");
    /* Clean frame */
		VideoTranscoderCleanFrame(vtc);
  }


	/* Depending on the code */
	if (codec & AST_FORMAT_H263)
	{
		/* Check codec */
		VideoTranscoderSetDecoder(vtc,CODEC_ID_H263);
		/* Depacketize */
		vtc->frameLen += rfc2190_append( vtc->frame,vtc->frameLen,buffer,bufferLen);

	} else if (codec & AST_FORMAT_H263_PLUS) {
		/* Check codec */
		VideoTranscoderSetDecoder(vtc,CODEC_ID_H263);
		/* Depacketize */
		vtc->frameLen += rfc2429_append(vtc->frame,vtc->frameLen,buffer,bufferLen);

	} else if (codec & AST_FORMAT_H264) {
		/* Check codec */
		VideoTranscoderSetDecoder(vtc,CODEC_ID_H264);
		/* Depacketize */
		vtc->frameLen += h264_append(vtc->frame,vtc->frameLen,vtc->frameSize,buffer,bufferLen);

	} else if (codec & AST_FORMAT_MPEG4) {
		/* Check codec */
		VideoTranscoderSetDecoder(vtc,CODEC_ID_MPEG4);
		/* Depacketize */
		vtc->frameLen += mpeg4_append(vtc->frame,vtc->frameLen,buffer,bufferLen);

	}else{
		ast_log(LOG_ERROR,"-Unknown codec [%d]\n",codec);
		return 0;
	}



	return 1;
}

struct VideoTranscoder * VideoTranscoderCreate(struct ast_channel *channel,char *format)
{
	char *i;

	/* Check params */
	if (strncasecmp(format,"h263",4) && strncasecmp(format,"h264",4))
		/* Only h263 or h264 output by now*/
		return NULL;

	/* Create transcoder */
	struct VideoTranscoder *vtc = (struct VideoTranscoder *) malloc(sizeof(struct VideoTranscoder));

  if ( vtc == NULL ) {
    	ast_log(LOG_DEBUG,"-Transcoder allocation failed\n" );
      return NULL ; 
  }

  memset(vtc, 0 , sizeof(struct VideoTranscoder));
	/* Set channel */
	vtc->channel	= channel;
  vtc->frame_count = 0 ;
	/* Set default parameters */
	vtc->format 	= 0;
	vtc->fps	= -1;
	vtc->bitrate 	= -1;
	vtc->qMin	= -1;
	vtc->qMax	= -1;
	vtc->gop_size	= -1;

	/* Get first parameter */
	i = strchr(format,'@');

	/* Parse param */
	while (i)
	{
		/* skip separator */
		i++;

		/* compare */
		if (strncasecmp(i,"qcif",4)==0)
		{
			/* Set qcif */
			vtc->format = 0;
		} else if (strncasecmp(i,"cif",3)==0) {
			/* Set cif */
			vtc->format = 1;
		} else if (strncasecmp(i,"fps=",4)==0) {
			/* Set fps */
			vtc->fps = atoi(i+4);
		} else if (strncasecmp(i,"kb=",3)==0) {
			/* Set bitrate */
			vtc->bitrate = atoi(i+3)*1024;
		} else if (strncasecmp(i,"qmin=",5)==0) {
			/* Set qMin */
			vtc->qMin = atoi(i+5);
		} else if (strncasecmp(i,"qmax=",5)==0) {
			/* Set qMax */
			vtc->qMax = atoi(i+5);
		} else if (strncasecmp(i,"gs=",3)==0) {
			/* Set gop size */
			vtc->gop_size = atoi(i+3);
		}

		/* Find next param*/
		i = strchr(i,'/');
	}


	/* Depending on the format */
	switch(vtc->format)
	{
		case 0:
			vtc->encoderWidth  = 176;
			vtc->encoderHeight = 144;
			break;
		case 1:
			vtc->encoderWidth  = 352;
			vtc->encoderHeight = 288;
			break;
	}	

	/* Malloc input frame */
	vtc->frameSize	= 65535;
	vtc->frameLen	= 0;
	vtc->frame 	= (uint8_t *)malloc(65535);

	/* Malloc output frame */
	vtc->bufferSize	= 65535;
	vtc->bufferLen	= 0;
	vtc->buffer 	= (uint8_t *)malloc(65535);

	/* Malloc decodec pictures */
	vtc->bufPicture = (uint8_t *)malloc(1179648); /* Max YUV 1024x768 */

	/* First input frame */
	vtc->newPic	= 0;
	vtc->end 	= 0;

	/* Alloc context */
  vtc->decoderCtx = avcodec_alloc_context();
  vtc->encoderCtx = avcodec_alloc_context();

	/* Allocate pictures */
  vtc->picture = avcodec_alloc_frame();

	/* Find encoder */
	if (!strncasecmp(format,"h263",4))
	{
		/* H263 encoder */
		vtc->encoder = avcodec_find_encoder(CODEC_ID_H263); 
		/* Set rfc 2490 payload */
		vtc->encoderFormat = AST_FORMAT_H263_PLUS;
		/* Flags */
		vtc->encoderCtx->mb_decision = FF_MB_DECISION_SIMPLE;
		vtc->encoderCtx->flags |= CODEC_FLAG_PASS1;                 //PASS1
		vtc->encoderCtx->flags &= ~CODEC_FLAG_H263P_UMV;            //unrestricted motion vector
		vtc->encoderCtx->flags &= ~CODEC_FLAG_4MV;                  //advanced prediction
		vtc->encoderCtx->flags &= ~CODEC_FLAG_H263P_SLICE_STRUCT;
	} else if (!strncasecmp(format,"h264",4)) {
		/* H264 encoder */
		vtc->encoder = avcodec_find_encoder(CODEC_ID_H264); 
		/* Set rfc payload */
		vtc->encoderFormat = AST_FORMAT_H264;
		/* Add x4->params.i_slice_max_size     = 1350; in X264_init function of in libavcodec/libx264.c */
		/* Fast encodinf parameters */
		vtc->encoderCtx->refs = 1;
		vtc->encoderCtx->scenechange_threshold = 0;
		vtc->encoderCtx->me_subpel_quality = 0;
		vtc->encoderCtx->partitions = X264_PART_I8X8 | X264_PART_I8X8;
		vtc->encoderCtx->me_method = ME_EPZS;
		vtc->encoderCtx->trellis = 0;
    vtc->encoderCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    vtc->encoderCtx->rtp_payload_size = 1300 ;
	}	

	ast_log(LOG_DEBUG,"-Transcoder [c=%d,f=%d,fps=%d,kb=%d,qmin=%d,qmax=%d,gs=%d]\n",vtc->encoderFormat,vtc->format,vtc->fps,vtc->bitrate,vtc->qMin,vtc->qMax,vtc->gop_size);

	/* No decoder still */
	vtc->decoder = NULL;
	vtc->decoderOpened = 0;

	/* No resize */
	vtc->resizeCtx		= NULL;
	vtc->resizeWidth	= 0;
	vtc->resizeHeight	= 0;

	/* Bicubic by default */
	vtc->resizeFlags	= SWS_BICUBIC;

	/* Picture data */
	vtc->encoderCtx->pix_fmt 	= PIX_FMT_YUV420P;
	vtc->encoderCtx->width		= vtc->encoderWidth;
	vtc->encoderCtx->height 	= vtc->encoderHeight;

	/* fps*/
	if (vtc->fps>0)
		/* set encoder params*/
    vtc->encoderCtx->time_base    	    = (AVRational){1,vtc->fps};/* frames per second */

  /* Bitrate */
	if (vtc->bitrate>0 && vtc->fps>0)
	{
		/* Set encoder params */
		vtc->encoderCtx->bit_rate           = vtc->bitrate;
    vtc->encoderCtx->bit_rate_tolerance = vtc->bitrate*av_q2d(vtc->encoderCtx->time_base) + 1;
	}


	/* gop size */
	if (vtc->gop_size>0)
		/* set encoder params*/
    vtc->encoderCtx->gop_size           = vtc->gop_size; // about one Intra frame per second

	/* Bitrate */
	if (vtc->bitrate>0)
	{
		/* set encoder params*/
		vtc->encoderCtx->rc_min_rate        = vtc->bitrate;
		vtc->encoderCtx->rc_max_rate        = vtc->bitrate;
	}	

	/* qMin */
	if (vtc->qMin>0)
		vtc->encoderCtx->mb_qmin = vtc->encoderCtx->qmin= vtc->qMin;
	/* qMax */
	if (vtc->qMax>0)
		vtc->encoderCtx->mb_qmax = vtc->encoderCtx->qmax= vtc->qMax;

  /* Video quality */
  vtc->encoderCtx->rc_buffer_size     = vtc->bufferSize;
  vtc->encoderCtx->rc_qsquish         = 0; //ratecontrol qmin qmax limiting method.
  vtc->encoderCtx->max_b_frames       = 0;
	vtc->encoderCtx->me_range 	    = 24;
	vtc->encoderCtx->max_qdiff 	    = 31;
  vtc->encoderCtx->i_quant_factor     = (float)-0.6;
  vtc->encoderCtx->i_quant_offset     = (float)0.0;
	vtc->encoderCtx->qcompress	    = 0.6;
	/* Open encoder */
	vtc->encoderOpened = avcodec_open(vtc->encoderCtx, vtc->encoder) != -1;

	/* If not opened correctly */
	if (!vtc->encoderOpened)
	{
		/* Error */
		ast_log(LOG_ERROR,"Error opening encoder\n");
		/* Destroy it */
		VideoTranscoderDestroy(vtc);
		/* Exit */
		return NULL;	
	}

	/* Return encoder */
	return vtc;
}

int VideoTranscoderSetResize(struct VideoTranscoder *vtc)
{
	/* If already resizing that size */
	if (vtc->decoderCtx->width == vtc->encoderCtx->width  && vtc->decoderCtx->height == vtc->encoderCtx->height )
		/* Nothing to do */
		return 1;

	/* if got contex */
	if (vtc->resizeCtx)
		/* Free it */
		sws_freeContext(vtc->resizeCtx);

  if ( !(vtc->resizePicture= avcodec_alloc_frame() )){
    ast_log(LOG_ERROR,"Error allocate frame !\n");
    return 0;
  }

	/* Get new context */
	vtc->resizeCtx = sws_getContext(vtc->decoderCtx->width, vtc->decoderCtx->height, 
                                  PIX_FMT_YUV420P, 
                                  vtc->encoderCtx->width, vtc->encoderCtx->height, 
                                  PIX_FMT_YUV420P, vtc->resizeFlags, NULL, NULL, NULL);

	/* Check */
	if (!vtc->resizeCtx){
    ast_log(LOG_ERROR,"Error allocate context !\n");
    return 0;
  }

	/* exit */
	return 1;
}

int VideoTranscoderEncodeFrame(	struct VideoTranscoder *vtc)
{

  AVFrame* picture = vtc->picture  ;
  /* Do we need to resize the image */
  if ( vtc->decoderCtx->width!=vtc->encoderWidth || vtc->decoderCtx->height!=vtc->encoderHeight)
  {
    //ast_log(LOG_DEBUG,"Encoded frame bad size in[%d x %d] -> [%d x %d] \n",
    //        vtc->decoderCtx->width,vtc->decoderCtx->height,vtc->encoderWidth,vtc->encoderHeight);

    /* Set size */
    if (!VideoTranscoderSetResize(vtc))
      /* Next frame */
      return 0 ;

    /* Resize frame */
    sws_scale(vtc->resizeCtx, vtc->picture->data , vtc->picture->linesize, 0, 
              vtc->decoderCtx->height, vtc->resizePicture->data, vtc->resizePicture->linesize);
    
    picture = vtc->resizePicture ;
  } 

  /* Encode */
  vtc->bufferLen = avcodec_encode_video(vtc->encoderCtx,vtc->buffer,vtc->bufferSize,picture);

  /* Debug */
  //ast_log(LOG_DEBUG,"Encoded frame [%d,0x%.2x,0x%.2x,0x%.2x,0x%.2x]\n",vtc->bufferLen,vtc->buffer[0],vtc->buffer[1],vtc->buffer[2],vtc->buffer[3]);
			
  return vtc->bufferLen;
		
}

// MP4 
static int mp4_rtp_write_audio(struct mp4track *t, struct ast_frame *f, int payload)
{
	/* Next sample */
	t->sampleId++;

	//ast_log(LOG_DEBUG, "Saving #%d:%d:%d %d samples %d size of audio, payload=%d\n", t->sampleId, t->track, t->hint, f->samples, f->datalen,payload);
	/* dump_buffer_hex("AMR buffer",f->data,f->datalen); */

	/* Add hint */

	MP4AddRtpHint(t->mp4, t->hint);

	/* Add rtp packet to hint track */
	MP4AddRtpPacket(t->mp4, t->hint, 0, 0);

	/* Save rtp specific payload header to hint */
	if (payload > 0)
		MP4AddRtpImmediateData(t->mp4, t->hint, AST_FRAME_GET_BUFFER(f), payload);

	/* Set which part of sample audio goes to this rtp packet */
	MP4AddRtpSampleData(t->mp4, t->hint, t->sampleId, 0, f->datalen - payload);

	/* Write rtp hint */
	MP4WriteRtpHint(t->mp4, t->hint, f->samples, 1);

	/* Write audio */
	MP4WriteSample(t->mp4, t->track, AST_FRAME_GET_BUFFER(f) + payload, f->datalen - payload, f->samples, 0, 0);

	return 0;
}

static void mp4_rtp_write_video_frame(struct mp4track *t, int samples)
{
	if ( option_debug > 4 )
    ast_log(LOG_DEBUG, "Saving #%d:%d:%d %d samples %d size of video\n", t->sampleId, t->track, t->hint, samples, t->length);

	/* Save rtp hint */
	MP4WriteRtpHint(t->mp4, t->hint, samples, t->intra);

	/* Save video frame */
	MP4WriteSample(t->mp4, t->track, t->frame, t->length, samples, 0, t->intra);
 
  if  (  t->vtc && t->trackH264 != -1 ){ 
    ast_log(LOG_DEBUG, "Saving h264 sample[%d] size[%d]\n", t->sampleId,  t->savMediaBuff.pos);

    if ( (t->vtc->frame_count  == 0) || ((t->vtc->frame_count % ( t->vtc->fps )) ==0) ) 
    {
      ast_log(LOG_DEBUG, "Saving h264 SPS size[%d]\n",  t->vtc->encoderCtx->extradata_size );
      MP4AddH264SequenceParameterSet(t->mp4, t->trackH264,t->vtc->encoderCtx->extradata, t->vtc->encoderCtx->extradata_size);
    }

    MP4WriteSample(t->mp4, t->trackH264, t->savMediaBuff.buffer ,  t->savMediaBuff.pos , samples, 0, t->intra);
    t->savMediaBuff.pos = 0 ;
    t->vtc->frame_count ++ ;
  }

}

static int mp4_rtp_write_video(struct mp4track *t, struct ast_frame *f, int payload, bool intra, int skip, const unsigned char * prependBuffer, int prependLength)
{
	/* rtp mark */
	const bool mBit = f->subclass & 0x1;

  if ( mBit  &&  t->vtc && t->trackH264 != -1 ){ 
		/* Decode frame */
		VideoTranscoderDecodeFrame(t->vtc);
    /* Clean frame */
		VideoTranscoderCleanFrame(t->vtc);
		/* Encode */
    VideoTranscoderEncodeFrame(t->vtc);
    /* Start Code */
    char tmp[FRAME_SIZE] ;
		memcpy(tmp, (char*)"\0\0\1",3);
    memcpy(&tmp[3],t->vtc->buffer,  t->vtc->bufferLen);
		t->vtc->bufferLen+=3 ;
    /* save buffer */
    t->mediaBuff.pos = t->vtc->bufferLen ;
    t->mediaBuff.bufferSize = t->vtc->bufferLen ;
    t->mediaBuff.buffer = tmp ;
    CloneBuffer(&t->savMediaBuff , &t->mediaBuff );
    /* Clean frame */
		VideoTranscoderCleanFrame(t->vtc);
  } 

	/* If it's the first packet of a new frame */
	if (t->first) {
		/* If we hava a sample */
		if (t->sampleId > 0) {
			/* Save frame */
			mp4_rtp_write_video_frame(t, f->samples);
			/* Reset buffer length */
			t->length = 0;
		}
		/* Reset first mark */
		t->first = 0;

		/* Save intra flag */
		t->intra = intra;

		/* Next frame */
		t->sampleId++;

		if ( option_debug > 4 )
      ast_log(LOG_DEBUG, "New video hint [%d,%d,%d,%d]\n",intra,payload,skip,prependLength);

		/* Add hint */
		MP4AddRtpHint(t->mp4, t->hint);
	}

	/* Add rtp packet to hint track */
	MP4AddRtpPacket(t->mp4, t->hint, mBit, 0);

	/* Save rtp specific payload header to hint */
	if (payload > 0)
		MP4AddRtpImmediateData(t->mp4, t->hint, AST_FRAME_GET_BUFFER(f), payload);




	/* If we have to prepend */
	if (prependLength)
	{
		/* Prepend data to video buffer */
		memcpy(t->frame + t->length, (char*)prependBuffer, prependLength);

		/* Inc length */
		t->length += prependLength;
	}
#if 0
	/* Set hint reference to video data */
	MP4AddRtpSampleData(t->mp4, t->hint, t->sampleId, (u_int32_t) t->length, f->datalen - payload - skip);

	/* Copy the video data to buffer */
	memcpy(t->frame + t->length, AST_FRAME_GET_BUFFER(f) + payload + skip, f->datalen - payload - skip);

	/* Increase stored buffer length */
	t->length += f->datalen - payload - skip;
#else
  /* append */
  uint8_t *buffer = AST_FRAME_GET_BUFFER(f) ;
  uint32_t bufferLen = f->datalen ;
  int      codec =  f->subclass ;
	/* Set hint reference to video data */
	MP4AddRtpSampleData(t->mp4, t->hint, t->sampleId, (u_int32_t) t->length+prependLength, f->datalen - payload - skip);


	/* Depending on the code */
	if (codec & AST_FORMAT_H263)
	{
		/* Depacketize */
		t->length += rfc2190_append( t->frame,t->length,buffer,bufferLen);
	} else if (codec & AST_FORMAT_H263_PLUS) {
		/* Depacketize */
		t->length +=  rfc2429_append(t->frame,t->length,buffer,bufferLen);
	} else if (codec & AST_FORMAT_H264) {
		/* Depacketize */
		t->length += h264_append(t->frame,t->length,FRAME_SIZE,buffer,bufferLen);
	} else if (codec & AST_FORMAT_MPEG4) {
		/* Depacketize */
		t->length += mpeg4_append(t->frame,t->length,buffer,bufferLen);
	}else{
		ast_log(LOG_ERROR,"-Unknown codec [%d]\n",codec);
		return 0;
	}


#endif
	/* If it's the las packet in a frame */
	if (mBit)
		/* Set first mark */
		t->first = 1;

	return 0;
}

struct mp4rtp {
	struct ast_channel *chan;
	MP4FileHandle mp4;
	MP4TrackId hint;
	MP4TrackId track;
	unsigned int timeScale;
	unsigned int sampleId;
	unsigned short numHintSamples;
	unsigned short packetIndex;
	unsigned int frameSamples;
	int frameSize;
	MP4Timestamp frameTime;
	int frameType;
	int frameSubClass;
	char *name;
	char *src;
	unsigned char type;

};

static void mp4_send_h264_sei(struct mp4rtp *p)
{
	unsigned char buffer[PKT_SIZE];
        struct ast_frame *f = (struct ast_frame *) buffer;
	uint8_t **sequenceHeader;
	uint8_t **pictureHeader;
	uint32_t *pictureHeaderSize;
	uint32_t *sequenceHeaderSize;
	uint32_t i;
	uint8_t* data;
	uint32_t dataLen;

	/* Get SEI information */
	MP4GetTrackH264SeqPictHeaders(p->mp4, p->track, &sequenceHeader, &sequenceHeaderSize, &pictureHeader, &pictureHeaderSize);

	/* Unset */
	memset(f, 0, PKT_SIZE);

	/* Let mp4 lib allocate memory */
	AST_FRAME_SET_BUFFER(f,f,PKT_OFFSET,PKT_PAYLOAD);
	f->src = strdup(p->src);

	/* Set type */
	f->frametype = p->frameType;
	f->subclass = p->frameSubClass;

	f->delivery.tv_usec = 0;
	f->delivery.tv_sec = 0;
	/* Don't free the frame outside */
	f->mallocd = 0;

	/* Get data pointer */
	data = AST_FRAME_GET_BUFFER(f);
	/* Reset length */
	dataLen = 0;

	/* Send sequence headers */
	i=0;

	/* Check we have sequence header */
	if (sequenceHeader)
		/* Loop array */
		while(sequenceHeader[i] && sequenceHeaderSize[i])
		{
			/* Check if it can be handled in a single packeti */
			if (sequenceHeaderSize[i]<1400)
			{
				/* If there is not enought length */
				if (dataLen+sequenceHeaderSize[i]>1400)
				{
					/* Set data length */
					f->datalen = dataLen;
					/* Write frame */
					ast_write(p->chan, f);
					/* Reset data */
					dataLen = 0;
				}
				/* Copy data */
				memcpy(data+dataLen,sequenceHeader[i],sequenceHeaderSize[i]);	
				/* Increase pointer */
				dataLen+=sequenceHeaderSize[i];
			}
			/* Free memory */
			free(sequenceHeader[i]);
			/* Next header */
			i++;
		}

	/* If there is still data */
	if (dataLen>0)
	{
		/* Set data length */
		f->datalen = dataLen;
		/* Write frame */
		ast_write(p->chan, f);
		/* Reset data */
		dataLen = 0;
	}

	/* Send picture headers */
	i=0;

	/* Check we have picture header */
	if (pictureHeader)
		/* Loop array */
		while(pictureHeader[i] && pictureHeaderSize[i])
		{
			/* Check if it can be handled in a single packeti */
			if (pictureHeaderSize[i]<1400)
			{
				/* If there is not enought length */
				if (dataLen+pictureHeaderSize[i]>1400)
				{
					/* Set data length */
					f->datalen = dataLen;
					/* Write frame */
					ast_write(p->chan, f);
					/* Reset data */
					dataLen = 0;
				}
				/* Copy data */
				memcpy(data+dataLen,pictureHeader[i],pictureHeaderSize[i]);	
				/* Increase pointer */
				dataLen+=pictureHeaderSize[i];
			}
			/* Free memory */
			free(pictureHeader[i]);
			/* Next header */
			i++;
		}

	/* If there is still data */
	if (dataLen>0)
	{
		/* Set data length */
		f->datalen = dataLen;
		/* Write frame */
		ast_write(p->chan, f);
		/* Reset data */
		dataLen = 0;
	}

	/* Free data */
	if (pictureHeader)
		free(pictureHeader);
	if (sequenceHeader)
		free(sequenceHeader);
	if (sequenceHeaderSize)
		free(sequenceHeaderSize);
	if (pictureHeaderSize)
		free(pictureHeaderSize);

}
static MP4Timestamp mp4_rtp_get_next_frame_time(struct mp4rtp *p)
{
	MP4Timestamp ts = MP4GetSampleTime(p->mp4, p->hint, p->sampleId);
	//Check it
	if (ts==MP4_INVALID_TIMESTAMP)
		//Return it
		return ts;
	//Convert to miliseconds
	ts = MP4ConvertFromTrackTimestamp(p->mp4, p->hint, ts, 1000);

	//Get next timestamp
	return ts;
}

static MP4Timestamp mp4_rtp_read(struct mp4rtp *p)
{

	unsigned char buffer[PKT_SIZE];
	struct ast_frame *f = (struct ast_frame *) buffer;
	int last = 0;
	int first = 0;
	uint8_t* data;

	/* If it's first packet of a frame */
	if (!p->numHintSamples) {
		/* Get number of rtp packets for this sample */
		if (!MP4ReadRtpHint(p->mp4, p->hint, p->sampleId, &p->numHintSamples)) {
			ast_log(LOG_DEBUG, "MP4ReadRtpHint failed [%d,%d]\n", p->hint,p->sampleId);
			return MP4_INVALID_TIMESTAMP;
		}

		/* Get size of sample */
		p->frameSize = MP4GetSampleSize(p->mp4, p->hint, p->sampleId);

                /* Get duration for this sample */
		p->frameSamples = MP4GetSampleDuration(p->mp4, p->hint, p->sampleId);

		/* Get sample timestamp */
		p->frameTime = MP4GetSampleTime(p->mp4, p->hint, p->sampleId);
		/*  Convert to miliseconds */
		p->frameTime = MP4ConvertFromTrackTimestamp(p->mp4, p->hint, p->frameTime, 1000);

		/* Set first flag */
		first = 1;

		/* Check if it is H264 and it is a Sync frame*/
		if (p->frameSubClass==AST_FORMAT_H264 && MP4GetSampleSync(p->mp4,p->track,p->sampleId))
			/* Send SEI info */
			mp4_send_h264_sei(p);
	}

	/* if it's the last */
	if (p->packetIndex + 1 == p->numHintSamples)
		last = 1;

	/* Unset */
	memset(f, 0, PKT_SIZE);

	/* Let mp4 lib allocate memory */
	AST_FRAME_SET_BUFFER(f,f,PKT_OFFSET,PKT_PAYLOAD);
	f->src = strdup(p->src);

	/* Set type */
	f->frametype = p->frameType;
	f->subclass = p->frameSubClass;

	f->delivery.tv_usec = 0;
	f->delivery.tv_sec = 0;
	/* Don't free the frame outside */
	f->mallocd = 0;

	/* If it's video set the mark of last rtp packet */
	if (f->frametype == AST_FRAME_VIDEO)
	{
		/* Set mark bit */
		f->subclass |= last;
		/* If it's the first packet of the frame */
		if (first)
			/* Set number of samples */
			f->samples = MP4ConvertFromTrackTimestamp(p->mp4, p->hint, p->frameSamples, 90000);
	} else {
		/* Set number of samples */
		f->samples =  MP4ConvertFromTrackTimestamp(p->mp4, p->hint, p->frameSamples, 8000);
	}

	/* Get data pointer */
	data = AST_FRAME_GET_BUFFER(f);

	/* Read next rtp packet */
	if (!MP4ReadRtpPacket(
				p->mp4,				/* MP4FileHandle hFile */
				p->hint,			/* MP4TrackId hintTrackId */
				p->packetIndex++,		/* u_int16_t packetIndex */
				(u_int8_t **) &data,		/* u_int8_t** ppBytes */
				(u_int32_t *) &f->datalen,	/* u_int32_t* pNumBytes */
				0,				/* u_int32_t ssrc DEFAULT(0) */
				0,				/* bool includeHeader DEFAULT(true) */
				1				/* bool includePayload DEFAULT(true) */
			)) {
		ast_log(LOG_DEBUG, "Error reading packet [%d,%d]\n", p->hint, p->track);
		return MP4_INVALID_TIMESTAMP;
	}

	/* Write frame */
	ast_write(p->chan, f);

	/* Are we the last packet in a hint? */
	if (last) {
		/* The first hint */
		p->packetIndex = 0;
		/* Go for next sample */
		p->sampleId++;
		p->numHintSamples = 0;
                /* Get next sample time*/
                return mp4_rtp_get_next_frame_time(p);
	}

	/* Return this frame timestamp */
	return p->frameTime;
}

static int mp4_play(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	struct mp4rtp audio = { chan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0 };
	struct mp4rtp video = { chan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0 };
	MP4FileHandle mp4;
	MP4TrackId hintId;
	MP4TrackId trackId;
	MP4Timestamp audioNext = MP4_INVALID_TIMESTAMP;
	MP4Timestamp videoNext = MP4_INVALID_TIMESTAMP;
	int t = 0;
	int i = 0;
	struct ast_frame *f = NULL;
	char src[128];
	int res = 0;
	struct timeval tv ;

	char *parse;
	int numberofDigits = -1;
	char *varName = NULL;
	char *stopChars = NULL;
	char dtmfBuffer[MAX_DTMF_BUFFER_SIZE];
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];

  int VideoNativeID[NATIVE_VIDEO_CODEC_LAST] ;
  int audioBestId = NO_CODEC;
  int audioULAWId = NO_CODEC;  
  int audioLastId = NO_CODEC;  
  int videoBestId = NO_CODEC;
  char *name      = NULL;
  memset( VideoNativeID , NO_CODEC , NATIVE_VIDEO_CODEC_LAST*sizeof(int) ) ;

	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(filename); AST_APP_ARG(options););

	/* Check for data */
	if (!data || ast_strlen_zero(data)) {
                ast_log(LOG_WARNING, "mp4play requires an argument (filename)\n");
                return -1;
        }

	ast_log(LOG_DEBUG, "mp4play %s\n", (char *)data);

	/* Set random src */
	snprintf(src, 128, "mp4play%08lx", ast_random());
	audio.src = src;
	video.src = src;

	/* Reset dtmf buffer */
	memset(dtmfBuffer,0,MAX_DTMF_BUFFER_SIZE);

	/* Lock module */
	u = ast_module_user_add(chan);

	/* Duplicate input */
	parse = ast_strdup(data);

	/* Get input data */
	AST_STANDARD_APP_ARGS(args, parse);

	/* Parse input data */
	if (!ast_strlen_zero(args.options) &&
			ast_app_parse_options(mp4play_exec_options, &opts, opt_args, args.options)) {
		ast_log(LOG_WARNING, "mp4play cannot partse options\n");
		res = -1;
		goto clean;
	}

	/* Check filename */
	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "mp4play requires an argument (filename)\n");
		res = -1;
		goto clean;
	}

	/* If we have DTMF number of digits options chek it */
	if (ast_test_flag(&opts, OPT_NOOFDTMF) && !ast_strlen_zero(opt_args[OPT_ARG_NOOFDTMF])) {

		/* Get number of digits to wait for */
		numberofDigits = atoi(opt_args[OPT_ARG_NOOFDTMF]);

		/* Check valid number */
		if (numberofDigits<=0) {
			ast_log(LOG_WARNING, "mp4play does not accept n(%s), hanging up.\n", opt_args[OPT_ARG_NOOFDTMF]);
			res = -1;
			goto clean;
		}

		/* Check valid sizei */ 
		if (numberofDigits>MAX_DTMF_BUFFER_SIZE-1) {
			numberofDigits = MAX_DTMF_BUFFER_SIZE-1;
			ast_log(LOG_WARNING, "mp4play does not accept n(%s), buffer is too short cutting to %d .\n", opt_args[OPT_ARG_NOOFDTMF],MAX_DTMF_BUFFER_SIZE-1);
		}

		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Setting number of digits to %d seconds.\n", numberofDigits);
	}

	/* If we have DTMF set variable otpion chekc it */
	if (ast_test_flag(&opts, OPT_DFTMINTOVAR) && !ast_strlen_zero(opt_args[OPT_ARG_DFTMINTOVAR])) {

		/* Get variable name */
		varName = opt_args[OPT_ARG_DFTMINTOVAR];

		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Setting variable name to %s .\n", varName);
	}

	/* If we have DTMF stop digit optiont check it */
        if (ast_test_flag(&opts, OPT_STOPDTMF) && !ast_strlen_zero(opt_args[OPT_ARG_STOPDTMF])) {

		/* Get stop digits */
                stopChars = opt_args[OPT_ARG_STOPDTMF];

                if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Stop chars are %s.\n",stopChars);
        }

	/* Open mp4 file */
	mp4 = MP4Read((char *) args.filename);

	/* If not valid */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
		ast_log(LOG_WARNING, "Invalid file handle for %s\n",(char *) args.filename);
		/* exit */
		res = -1;
		goto clean;
	}

	/* Get the first hint track */
	hintId = MP4FindTrackId(mp4, i++, MP4_HINT_TRACK_TYPE, 0);

	/* Iterate hint tracks */
	while (hintId != MP4_INVALID_TRACK_ID) {

		ast_log(LOG_DEBUG, "found hint track %d\n", hintId);

		/* Get asociated track */
		trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);

		/* Check it's good */
		if (trackId != MP4_INVALID_TRACK_ID) {

			/* Get type */
			const char *type = MP4GetTrackType(mp4, trackId);

			ast_log(LOG_DEBUG, "track %d %s\n", trackId, type);

			/* Check track type */
			if (strcmp(type, MP4_AUDIO_TRACK_TYPE) == 0) {
				/* it's audio */
				audio.mp4 = mp4;
				audio.hint = hintId;
				audio.track = trackId;
				audio.sampleId = 1;
				audio.packetIndex = 0;
				audio.frameType = AST_FRAME_VOICE;

				/* Get audio type */
				MP4GetHintTrackRtpPayload(mp4, hintId, &audio.name, &audio.type, NULL, NULL);

				/* Get time scale */
				audio.timeScale = MP4GetTrackTimeScale(mp4, hintId);

				/* Depending on the name */
				if (strcmp("PCMU", audio.name) == 0)
				{
					audio.frameSubClass = AST_FORMAT_ULAW;
					if (ast_set_write_format(chan, AST_FORMAT_ULAW))
						ast_log(LOG_WARNING, "mp4_play:	Unable to set write format to ULAW!\n");
				}
				else if (strcmp("PCMA", audio.name) == 0)
				{
					audio.frameSubClass = AST_FORMAT_ALAW;
					if (ast_set_write_format(chan, AST_FORMAT_ALAW))
						ast_log(LOG_WARNING, "mp4_play:	Unable to set write format to ALAW!\n");
				} 
				else if (strcmp("AMR", audio.name) == 0)
				{
					audio.frameSubClass = AST_FORMAT_AMRNB;
					if (ast_set_write_format(chan, AST_FORMAT_AMRNB))
						ast_log(LOG_WARNING, "mp4_play:	Unable to set write format to AMR-NB!\n");
				}

			} else if (strcmp(type, MP4_VIDEO_TRACK_TYPE) == 0) {
				/* it's video */
				video.mp4 = mp4;
				video.hint = hintId;
				video.track = trackId;
				video.sampleId = 1;
				video.packetIndex = 0;
				video.frameType = AST_FRAME_VIDEO;

				/* Get video type */
				MP4GetHintTrackRtpPayload(mp4, hintId, &video.name, &video.type, NULL, NULL);

				/* Get time scale */
				video.timeScale = MP4GetTrackTimeScale(mp4, hintId);

				ast_log(LOG_DEBUG, "video.name %s\n", video.name);

				/* Depending on the name */
				if (strcmp("H263", video.name) == 0)
					video.frameSubClass = AST_FORMAT_H263;
				else if (strcmp("H263-1998", video.name) == 0)
					video.frameSubClass = AST_FORMAT_H263_PLUS;
				else if (strcmp("H263-2000", video.name) == 0)
					video.frameSubClass = AST_FORMAT_H263_PLUS;
				else if (strcmp("H264", video.name) == 0)
					video.frameSubClass = AST_FORMAT_H264;
			}
		}

		/* Get the next hint track */
		hintId = MP4FindTrackId(mp4, i++, MP4_HINT_TRACK_TYPE, 0);
	}

	/* If we have audio */
	if (audio.name)
		/* Get next audio time */
		audioNext = mp4_rtp_get_next_frame_time(&audio);

	/* If we have video */
	if (video.name)
		/* Send next video time */
		videoNext = mp4_rtp_get_next_frame_time(&video);

	/* Calculate start time */
	tv = ast_tvnow();

        /* Set time counter */
        t = 0;

	/* Wait control messages or finish of both streams */
	while (audioNext!=MP4_INVALID_TIMESTAMP && videoNext!=MP4_INVALID_TIMESTAMP) {
		/* Get next time */
		if (audioNext<videoNext)
			t = audioNext;
		else
			t = videoNext;

                /* Calculate elapsed */
		int now = ast_tvdiff_ms(ast_tvnow(),tv);

		/* Read from channel and wait timeout */
		while (t > now) {
			/* Wait */
			int ms = ast_waitfor(chan, t-now);

			/* if we have been hang up */
			if (ms < 0) 
				/* exit */
				goto end;

			/* if we have received something on the channel */
			if (ms > 0) {
				/* Read frame */
				f = ast_read(chan);

				/* If failed */
				if (!f) 
					/* exit */
					goto end;

				/* If it's a dtmf */
				if (f->frametype == AST_FRAME_DTMF) {

					/* Stop flag */
					bool stop = false;

					/* Get DTMF char */
					char dtmf[2];
					dtmf[0] = f->subclass;
					dtmf[1] = 0;

					/* Check if it's in the stop char digits */
					if (stopChars && strchr(stopChars,dtmf[0])) {
						/* Clean DMTF input */
						strcpy(dtmfBuffer,dtmf);
						/* Stop */
						stop = true;
						/* Continue after exit */
						res = 0;
						/* Log */
						ast_log( LOG_WARNING, "mp4play - found stop char %s\n",dtmf);
					/* Check if we have to append the DTMF and wait for more than one digit */
					} else if (numberofDigits>0) {
						/* Append to the DTMF buffer */
						strcat(dtmfBuffer,dtmf);
						/* Check length */
						if (strlen(dtmfBuffer)>=numberofDigits) {
							/* Continue after exit */
							res = 0;
							/* Stop */
							stop = true;	
						}
					/* Check for dtmf extension in context */
					} else if (ast_exists_extension(chan, chan->context, dtmf, 1, NULL)) {
						/* Set extension to jump */
						res = f->subclass;
						/* Clean DMTF input */
						strcpy(dtmfBuffer,dtmf);
						/* End */
						stop = true;
					}
					
					/* If we have to stop */
					if (stop) {
						/* Check DTMF variable`option*/
						if (varName)
							/* Build variable */
							pbx_builtin_setvar_helper(chan, varName, dtmfBuffer);
						/* Free frame */
						ast_frfree(f);
						/* exit */
						goto end;
					}
				} 

				/* Free frame */
				ast_frfree(f);
			}

			/* Calculate new time */
			now = ast_tvdiff_ms(ast_tvnow(),tv);
		}

		/* if we have to send audio */
		if (audioNext<=t && audio.name)
			audioNext = mp4_rtp_read(&audio);

		/* or video */
		if (videoNext<=t && video.name)
			videoNext = mp4_rtp_read(&video);
	}

end:
	/* Log end */
	ast_log(LOG_DEBUG, "<app_mp4");

	/* Close file */
	MP4Close(mp4,0);

clean:
	/* Unlock module*/
	ast_module_user_remove(u);

	/* Free datra*/
	free(parse);

	/* Exit */
	return res;
}

static int mp4_save(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	struct ast_frame *f = NULL;
	struct mp4track audioTrack;
	struct mp4track videoTrack;
	MP4FileHandle mp4;
	MP4TrackId audio = -1;
	MP4TrackId video = -1;
	MP4TrackId videoH264 = -1;
	MP4TrackId hintAudio = -1;
	MP4TrackId hintVideo = -1;
	unsigned char type = 0;
	char *params = NULL;
	int audio_payload = 0, video_payload = 0;
	int loopVideo = 0;
	int waitVideo = 0;
  struct VideoTranscoder* h264trancoder = NULL ;
  /* reset tracks */
  memset(&audioTrack,0,sizeof(audioTrack));
  memset(&videoTrack,0,sizeof(videoTrack));
  char h264Param[]="h264@cif/fps=30/kb=384/qmin=4/qmax=32/gs=250";
  int haveAudio           =  chan->nativeformats & AST_FORMAT_AUDIO_MASK ;
  int haveVideo           =  chan->nativeformats & AST_FORMAT_VIDEO_MASK ;  
  int haveText            =  chan->nativeformats & AST_FORMAT_TEXT_MASK ;  
  int synchroVideo = 1;
  int saveText     = 1; 

	/* Check for file */
	if (!data)
		return -1;
	/* Check for params */
	params = strchr(data,'|');

	/* If there are params */
	if (params)
	{
		/* Remove from file name */
		*params = 0;

		/* Increase pointer */
		params++;

		/* Check video loopback */
		if (strchr(params,'v'))
		{
			/* Enable video loopback */
			loopVideo = 1;
		}

		/* Check video waiting */
		if (strchr(params,'V'))
		{
			/* Enable video loopback & waiting*/
			loopVideo = 1;
			waitVideo = 1;
		}
	
		if (strchr(params,'s'))
		{
			synchroVideo = 1;
		}

		if ( strchr(params, 't') )
		{
		    saveText = 1;
		}
	}

  ast_log(LOG_DEBUG, ">mp4save [%s,%s] loopVideo[%d] waitVideo[%d] "
          " synchroVideo[%d] saveText[%d] audio[%s] video[%s] txt[%s]\n",
          (char*)data,params,
          loopVideo,waitVideo,synchroVideo,saveText,
          (haveAudio)?"OK":"NONE",
          (haveVideo)?"OK":"NONE",
          (haveText) ?"OK":"NONE"); 
	/* Create mp4 file */
	mp4 = MP4Create((char *) data, 0);

	/* If failed */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
		return -1;

	/* Lock module */
	u = ast_module_user_add(chan);

	if (ast_set_read_format(chan, AST_FORMAT_ULAW|AST_FORMAT_ALAW|AST_FORMAT_AMRNB))
		ast_log(LOG_WARNING, "mp4_save: Unable to set read format to ULAW|ALAW|AMRNB!\n");

	/* Send video update */
	ast_indicate(chan, AST_CONTROL_VIDUPDATE);

	/* Wait for data avaiable on channel */
	while (ast_waitfor(chan, -1) > -1)
	{

		/* Read frame from channel */
		f = ast_read(chan);

		/* if it's null */
		if (f == NULL)
			break;

		/* Check if we have to wait for video */
		if ((f->frametype == AST_FRAME_VOICE) && (!waitVideo)) 
		{
			/* Check if we have the audio track */
			if (audio == -1)
			{
				/* Check codec */
				if (f->subclass & AST_FORMAT_ULAW)
				{
					/* Create audio track */
					audio = MP4AddULawAudioTrack(mp4,8000);
					/* Set channel and sample properties */
					MP4SetTrackIntegerProperty(mp4, audio, "mdia.minf.stbl.stsd.ulaw.channels", 1);
                                        MP4SetTrackIntegerProperty(mp4, audio, "mdia.minf.stbl.stsd.ulaw.sampleSize", 8);

					/* Create audio hint track */
					hintAudio = MP4AddHintTrack(mp4, audio);
					/* Set payload type for hint track */
					type = 0;
					audio_payload = 0;
					MP4SetHintTrackRtpPayload(mp4, hintAudio, "PCMU", &type, 0, NULL, 1, 0);
				} else if (f->subclass & AST_FORMAT_ALAW) {
					/* Create audio track */
					audio = MP4AddALawAudioTrack(mp4,8000);
					/* Set channel and sample properties */
					MP4SetTrackIntegerProperty(mp4, audio, "mdia.minf.stbl.stsd.alaw.channels", 1);
                                        MP4SetTrackIntegerProperty(mp4, audio, "mdia.minf.stbl.stsd.alaw.sampleSize", 8);
					/* Create audio hint track */
					hintAudio = MP4AddHintTrack(mp4, audio);
					/* Set payload type for hint track */
					type = 8;
					audio_payload = 0;
					MP4SetHintTrackRtpPayload(mp4, hintAudio, "PCMA", &type, 0, NULL, 1, 0);
				} else if (f->subclass & AST_FORMAT_AMRNB) {
					/* Create audio track */
					audio = MP4AddAmrAudioTrack(mp4, 8000, 0, 0, 1, 0); /* Should check framesPerSample*/
					/* Create audio hint track */
					hintAudio = MP4AddHintTrack(mp4, audio);
					/* Set payload type for hint track */
					type = 98;
					audio_payload = 1;
					MP4SetHintTrackRtpPayload(mp4, hintAudio, "AMR", &type, 0, NULL, 1, 0);
					/* Unknown things */
					MP4SetAudioProfileLevel(mp4, 0xFE);
				} else {
					/* Unknown code free it*/
					ast_frfree(f);
					/* skip this one */
					continue;
				}

				/* Set struct info */
				audioTrack.mp4 = mp4;
				audioTrack.track = audio;
				audioTrack.hint = hintAudio;
				audioTrack.length = 0;
				audioTrack.sampleId = 0;
				audioTrack.first = 1;
			}

			/* Check we have audio track */
			if (audio)
				/* Save audio rtp packet */
				mp4_rtp_write_audio(&audioTrack, f, audio_payload);

		} else if (f->frametype == AST_FRAME_VIDEO) {
			/* No skip and no add */
			int skip = 0;
			unsigned char *prependBuffer = NULL;
			unsigned char *frame = AST_FRAME_GET_BUFFER(f);
			int prependLength = 0;
			int intra = 0;
			int first = 0;

			/* Check codec */
			if (f->subclass & AST_FORMAT_H263)
			{
				/* Check if it's an intra frame */
				intra = ((frame[1] & 0x10) != 0);
        // First check for PSC: Picture Start Code (22 bits) = '0000 0000 0000 0000 1000 00'
        if ( (f->datalen>7 && (frame[4] == 0) && (frame[5] == 0)  && ((frame[6] & 0xfc) == 0x80))){
            first = 1;
        }
				prependBuffer = NULL;
        prependLength = 0;
        /* Set payload and skip */
        video_payload = 4;
        skip = 0;
			} else if (f->subclass & AST_FORMAT_H263_PLUS) {
				/* Check if it's an intra frame */
				const unsigned char p = frame[0] & 0x04;
				const unsigned char v = frame[0] & 0x02;
				const unsigned char plen = ((frame[0] & 0x1 ) << 5 ) | (frame[1] >> 3);
				// const unsigned char pebit = frame[0] & 0x7; unused variable
				
				/* payload length */
				video_payload = 2;
				/* skip rest of headers */
				skip = plen + v;
				/* If its first packet of frame*/
				if (p)
				{
					/* it's first */
					first = 1;
					/* Check for intra in stream */
					intra = !(frame[4] & 0x02);
					/* Prepend open code */
					prependBuffer = (unsigned char*)"\0\0";
					prependLength = 2;
				}
			} else if (f->subclass & AST_FORMAT_H264) {
				/* Get packet type */
				const unsigned char nal = frame[0];
				const unsigned char type = nal & 0x1f;
				/* All intra & first*/
				intra = 1;
				first = 1;
				/* Check nal type */
				if (type<23) 
				{
					/* And add the data to the frame but not associated with the hint track */
					prependBuffer = (unsigned char*)"\0\0\1";
					prependLength = 3;
					/* Set payload and skip */
					video_payload = 0;
					skip = 0;
				}
			} else {
				/* Unknown code free it */
				ast_frfree(f);
				/* skip this one */
				continue;
			}
	
			/* Check if we have to wait for video */
			if (waitVideo)
			{
				/* If it's the first packet of an intra frame */
				if (first && intra)
				{
					/* no more waiting */
					waitVideo = 0;
				} else {
					/* free frame */
					ast_frfree(f);
					/* Keep on waiting */
					continue;
				}
			}

			/* Check if we have the video track */
			if (video == -1)
			{
				/* Check codec */
				if (f->subclass & AST_FORMAT_H263)
				{
          // Create h264 transcoder
          h264trancoder = VideoTranscoderCreate(chan,h264Param);
          if ( h264trancoder )
          {
            /* Should parse video packet to get this values */
            unsigned char AVCProfileIndication 	= 66;
            unsigned char AVCLevelIndication	= 13;
            unsigned char AVCProfileCompat		= 224;
            MP4Duration h264FrameDuration		  = 90000.0/30;
            /* Create video track */
            videoH264 = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, 352, 288, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
          }
					/* Create video track */
					video = MP4AddH263VideoTrack(mp4, 90000, 0, 352, 288, 0, 0, 0, 0);

					/* Create video hint track */
					hintVideo = MP4AddHintTrack(mp4, video);

					/* Set payload type for hint track */
					type = 34;

					MP4SetHintTrackRtpPayload(mp4, hintVideo, "H263", &type, 0, NULL, 1, 0);

				} else if (f->subclass & AST_FORMAT_H263_PLUS) {
          // Create h264 transcoder
          h264trancoder = VideoTranscoderCreate(chan,h264Param);
          if ( h264trancoder )
          {
            /* Should parse video packet to get this values */
            unsigned char AVCProfileIndication 	= 66;
            unsigned char AVCLevelIndication	= 13;
            unsigned char AVCProfileCompat		= 224;
            MP4Duration h264FrameDuration		  = 90000.0/30;
            /* Create video track */
            videoH264 = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, 176, 144, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
          }

					/* Create video track */
					video = MP4AddH263VideoTrack(mp4, 90000, 0, 176, 144, 0, 0, 0, 0);

					/* Create video hint track */
					hintVideo = MP4AddHintTrack(mp4, video);

					/* Set payload type for hint track */
					type = 96;

					MP4SetHintTrackRtpPayload(mp4, hintVideo, "H263-1998", &type, 0, NULL, 1, 0);

				}else if ( f->subclass & AST_FORMAT_H264) 
        {
					/* Should parse video packet to get this values */
					unsigned char AVCProfileIndication 	= 66;
					unsigned char AVCLevelIndication	= 13;
					unsigned char AVCProfileCompat		= 224;
					MP4Duration h264FrameDuration		= 1.0/30;
					/* Create video track */
					video = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, 176, 144, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
					/* Create video hint track */

					hintVideo = MP4AddHintTrack(mp4, video);

					/* Set payload type for hint track */
					type = 99;

					MP4SetHintTrackRtpPayload(mp4, hintVideo, "H264", &type, 0, NULL, 1, 0);

				}

				/* Set struct info */
				videoTrack.mp4       = mp4;
				videoTrack.track     = video;
        videoTrack.trackH264 = videoH264;
				videoTrack.hint      = hintVideo;
				videoTrack.length    = 0;
				videoTrack.sampleId  = 0;
				videoTrack.first     = 1;
        videoTrack.vtc       = h264trancoder ;
			}

			/* If we have created the track */
			if (video ){

        /* transcode */
        if ( videoTrack.vtc )
          VideoTranscoderWrite(videoTrack.vtc,f->subclass,AST_FRAME_GET_BUFFER(f),f->datalen,f->subclass & 1);

				/* Write rtp video packet */
				mp4_rtp_write_video(&videoTrack, f, video_payload, intra, skip, prependBuffer , prependLength);
      }
			/* If video loopback is activated */
			if (loopVideo)
			{
				/* Send it back */
				ast_write(chan, f);
				/* Don't delete it */
				f = NULL;
			}

		} else if (f->frametype == AST_FRAME_DTMF) {
			/* If it's the dtmf param */
			if (params && strchr(params,f->subclass))
			{
				/* free frame */
				ast_frfree(f);
				/* exit */
				break;
			}

		}

		/* If we have frame */
		if (f)
			/* free frame */
			ast_frfree(f);
	}

	/* Save last video frame if needed */
	if (videoTrack.sampleId > 0)
		/* Save frame */
		mp4_rtp_write_video_frame(&videoTrack, 0);

	/* Close file */
	MP4Close(mp4,0);
	/* Destroy transcoders */
	if (h264trancoder)
		VideoTranscoderDestroy(h264trancoder);
  if ( videoTrack.savMediaBuff.buffer )
    free( videoTrack.savMediaBuff.buffer );
	/* Unlock module*/
	ast_module_user_remove(u);

	printf("<mp4save\n");

	//Success
	return 0;
}

static int unload_module(void)
{
	int res = ast_unregister_application(app_play);
	res &= ast_unregister_application(app_save);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	/* Set log */
	av_log_set_callback(av_log_asterisk_callback);

	/* Init avcodec */
	avcodec_init();
	
	/* Register all codecs */	
	avcodec_register_all();
	int res = ast_register_application(app_save, mp4_save, syn_save, des_save);
	res &= ast_register_application(app_play, mp4_play, syn_play, des_play);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "MP4 applications");


