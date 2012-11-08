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

/*
 * Changelog:
 *
 *  15-01-2006
 *  	Code cleanup and ast_module_user_add added.
 *  	Thanxs Denis Smirnov.
 */

/*! \file
 *
 * \brief MP4 application -- save and play mp4 files
 *
 * \ingroup applications
 */
#include <sys/time.h>

#include <asterisk.h>
#ifdef MP4V2
#include <mp4v2/mp4v2.h>
#else
#include <mp4.h>
#include <mp4av.h>
#include <mp4av_h264.h>
#endif

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

#undef i6net
#undef i6net_lock
#ifndef i6net
#include <asterisk/speech.h>
#endif
#ifdef i6net_lock
#include <app_vxml.h>
#endif

#ifndef AST_FORMAT_AMRNB
#define AST_FORMAT_AMRNB 	(1 << 13)
#endif

#ifndef _STR_CODEC_SIZE
#define _STR_CODEC_SIZE         512
#endif

#define AST_FRAME_GET_BUFFER(fr)        ((unsigned char*)((fr)->data))


#define PKT_PAYLOAD	1450
#define PKT_SIZE 	(sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + PKT_PAYLOAD)
#define PKT_OFFSET	(sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET)
#define AST_MAX_TXT_SIZE 0x8000 
#define NO_CODEC         -1
#define MS_2_SEC         1000000   // Micro secondes -> Sec 

#define TIMEVAL_TO_MS( tv , ms ) \
  { \
    ms = ( tv.tv_sec * MS_2_SEC ) + tv.tv_usec ; \
  }

#define DIFF_MS( LastTv , CurrTv , msRes )            \
  { \
    long long LastMs = 0L ; \
    long long CurrMs = 0L ; \
    TIMEVAL_TO_MS( LastTv , LastMs ) ; \
    TIMEVAL_TO_MS( CurrTv , CurrMs ) ; \
    msRes = CurrMs - LastMs ; \
  }
#ifndef MIN
#define MIN(a,b) ((a) > (b) ? (b) : (a))
#endif
#ifndef ABS
#define ABS(a) ((a) >= 0 ? (a) : (-(a)))
#endif
/* ========================================================================= */
/* Structures et enums                                                       */
/* ========================================================================= */
#ifdef VIDEOCAPS
/*! \brief codec video dans le fichier 
 */
typedef enum 
{
  NATIVE_VIDEO_CODEC_H264 = 0 ,
  NATIVE_VIDEO_CODEC_H263P ,
  NATIVE_VIDEO_CODEC_H263 ,
  NATIVE_VIDEO_CODEC_LAST // Always last 
} NativeCodec;
#endif





static const char mark_cut_txt[]=" Buff too small supress end of text";
static const char h263VideoName[]="H.263" ; 
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


struct mp4track {
	MP4FileHandle mp4;
	MP4TrackId track;
	MP4TrackId hint;
	bool first;
	bool intra;
	unsigned char *frame;	
	int length;
	int sampleId;
	int timeScale;
};


enum {
        OPT_DFTMINTOVAR 	=	(1 << 0),
        OPT_NOOFDTMF 		=	(1 << 1),
	OPT_STOPDTMF		=	(1 << 2),
} mp4play_exec_option_flags;

enum {
        OPT_ARG_DFTMINTOVAR =          0,
        OPT_ARG_NOOFDTMF,
	OPT_ARG_STOPDTMF,
        /* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
} mp4play_exec_option_args;

AST_APP_OPTIONS(mp4play_exec_options, {
        AST_APP_OPTION_ARG('S', OPT_DFTMINTOVAR, OPT_ARG_DFTMINTOVAR),
        AST_APP_OPTION_ARG('n', OPT_NOOFDTMF, OPT_ARG_NOOFDTMF),
	AST_APP_OPTION_ARG('s', OPT_STOPDTMF, OPT_ARG_STOPDTMF),
});

#define H263_RTP_MAX_HEADER_LEN     8       // H263 header max length for RFC2190 (mode A) or RFC2429
#define MAX_H263_PACKET             32      // max number of RTP packet per H263 Video Frame
#define H263_FRAME_SIZE            (PKT_PAYLOAD - H263_HEADER_MODE_A_SIZE)







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
#define H263P_HEADER_SIZE		2
#define H263_HEADER_MODE_A_SIZE 4
#define H263_HEADER_MODE_B_SIZE 8
#define H263_HEADER_MODE_C_SIZE 12
static void suppressT140BOM(char* buff,size_t* sz );

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

static void SendVideoFrameH263(struct ast_channel *chan, uint8_t *data, uint32_t size, int first, int last , int fps)
{
	uint8_t frameBuffer[PKT_SIZE];
	struct ast_frame *send = (struct ast_frame *) frameBuffer;
	uint8_t *frameData = NULL;
 	unsigned char RFC2190_header[4] = {0} ;
	
  if(data[0] == 0x00 && data[1] == 0x00 && (data[2] & 0xfc)==0x80){ /* PSC */
		/* RFC 2190 -5.1 Mode A
			0                   1                   2                   3
			0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			|F|P|SBIT |EBIT | SRC |I|U|S|A|R      |DBQ| TRB |    TR         |
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		
		SRC : 3 bits
		Source format, bit 6,7 and 8 in PTYPE defined by H.263 [4], specifies
		the resolution of the current picture.
		
		I:  1 bit.
		Picture coding type, bit 9 in PTYPE defined by H.263[4], "0" is
		intra-coded, "1" is inter-coded.
		*/
		
		// PDATA[4] ======> Bits 3-10 of PTYPE
		uint8_t format, pict_type;
		
		// Source Format = 4,5,6
		format = (data[4] & 0x3C)>>2;
		// Picture Coding Type = 7
		pict_type = (data[4] & 0x02)>>1;
		// RTP mode A header
    ((uint32_t *)RFC2190_header)[1] = ntohl( (format <<5) | (pict_type << 4) );
	}

  uint32_t *p = (uint32_t *) data;
  int gob_num = (ntohl(*p) >> 10) & 0x1f;
  char *dat = (char *)data;	
  
  static uint32_t tr = 0; //Static to have it when needed for splitting into multiple
  static uint32_t sz = 0; //packets (a hack that works for one video connection
  //         only, must be stored elsewhere for more)

	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"Send video frame [%p,%d,%d,%d,0x%.2x,0x%.2x,0x%.2x,0x%.2x]\n",send,size,first,last,data[0],data[1],data[2],data[3]);

	/* Check size */
	if (size+2>PKT_PAYLOAD)
	{
		/* Error */
		ast_log(LOG_ERROR,"Send video frame too large [%d]\n",size);
		/* Exit */
		return ;
	}

	/* clean */
	memset(send,0,PKT_SIZE);

	/* Set frame data */
	AST_FRAME_SET_BUFFER(send,send,PKT_OFFSET,0);
	/* Get the frame pointer */
	frameData = AST_FRAME_GET_BUFFER(send);


	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"GOB number %d\n",gob_num);

  if(gob_num == 0)
  {
    /* Get relevant framedata and memorize it for later use */
    /* Get the "temporal reference" from the H.263 frameheader. */
    tr = ((dat[2] & 0x03) * 64) + ((dat[3] & 0xfc) / 4);
    /* Get the Imgesize from the H.263 frameheader. */
    sz = (dat[4] >> 2) & 0x07;
  }
  else
  {
    /* The memorized values from the frame start will be used */
  }
  /* Construct payload header.
     Set videosize and the temporal reference to that of the frame */
  ((uint32_t *)RFC2190_header)[0] = ntohl((sz << 21) | (tr & 0x000000ff));
  
	/* Set frame len*/
	send->datalen = size+4;
	/* Set header */
	memcpy(frameData, RFC2190_header, 4);
	/* Copy */
	memcpy(frameData+4, data, size);
	/* Set timestamp */
	send->samples = 90000/fps;

	/* Set video type */
	send->frametype = AST_FRAME_VIDEO;
	/* Set codec value */
	send->subclass = AST_FORMAT_H263 | last;
	/* Rest of values*/
	send->src = "mp4play";
	send->delivery = ast_tv(0, 0);
	/* Don't free the frame outrside */
	send->mallocd = 0;


	/* Send */
	//vtc->channel->tech->write_video(vtc->channel, send);
	ast_write(chan, send);
}


#define MAX_DTMF_BUFFER_SIZE 25

#ifdef i6net
void CreateHintTrack(MP4FileHandle mp4File, MP4TrackId mediaTrackId,
		     const char* payloadName, bool interleave, 
		     u_int16_t maxPayloadSize, bool doEncrypt)
{

  bool rc = FALSE;

  if (MP4GetTrackNumberOfSamples(mp4File, mediaTrackId) == 0) {
    fprintf(stderr, 
	    "%s: couldn't create hint track, no media samples\n", ProgName);
    MP4Close(mp4File);
    exit(EXIT_CREATE_HINT);
  }

  // vector out to specific hinters
  const char* trackType = MP4GetTrackType(mp4File, mediaTrackId);

  if (doEncrypt || MP4IsIsmaCrypMediaTrack(mp4File, mediaTrackId)) {

    ismacryp_session_id_t icSID;
    mp4av_ismacrypParams *icPp =  (mp4av_ismacrypParams *) malloc(sizeof(mp4av_ismacrypParams));
    memset(icPp, 0, sizeof(mp4av_ismacrypParams));

    if (!strcmp(trackType, MP4_AUDIO_TRACK_TYPE)) {
       if (ismacrypInitSession(&icSID, KeyTypeAudio) != 0) {
          fprintf(stderr, 
	      "%s: can't hint, error in init ismacryp session\n", ProgName);
          goto quit_error;
       }
    }
    else if (!strcmp(trackType, MP4_VIDEO_TRACK_TYPE)) {
       if (ismacrypInitSession(&icSID, KeyTypeVideo) != 0) {
          fprintf(stderr, 
	      "%s: can't hint, error in init ismacryp session\n", ProgName);
          goto quit_error;
       }
    }
    else {
      fprintf(stderr, 
	      "%s: can't hint track type %s\n", ProgName, trackType);
      goto quit_error;
    }
    
    // get all the ismacryp parameters needed by the hinters:
    if (ismacrypGetKeyCount(icSID, &(icPp->key_count)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting key count for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetKeyIndicatorLength(icSID, &(icPp->key_ind_len)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting key ind len for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }

    if (ismacrypGetKeyIndPerAU(icSID, &(icPp->key_ind_perau)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting key ind per au for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetSelectiveEncryption(icSID, &(icPp->selective_enc)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting selective enc for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetDeltaIVLength(icSID, &(icPp->delta_iv_len)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting delta iv len for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetIVLength(icSID, &(icPp->iv_len)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting iv len for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetScheme(icSID, (ismacryp_scheme_t *) &(icPp->scheme)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting scheme for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }
    if (ismacrypGetKey(icSID, 1,&(icPp->key_len),&(icPp->salt_len),
                       &(icPp->key),&(icPp->salt),&(icPp->key_life)) != 0) {
      fprintf(stderr, 
	      "%s: can't hint, error getting scheme for session %d\n", 
               ProgName, icSID);
      goto quit_error;
    }

    goto ok_continue;
    quit_error:
    ismacrypEndSession(icSID);
    MP4Close(mp4File);
    exit(EXIT_CREATE_HINT);
    ok_continue:

    if (!strcmp(trackType, MP4_AUDIO_TRACK_TYPE)) {
      rc = MP4AV_RfcCryptoAudioHinter(mp4File, mediaTrackId, 
                                      icPp, 
				      interleave, maxPayloadSize, 
				      "enc-mpeg4-generic");
      ismacrypEndSession(icSID);
    } else if (!strcmp(trackType, MP4_VIDEO_TRACK_TYPE)) {
      rc = MP4AV_RfcCryptoVideoHinter(mp4File, mediaTrackId, 
                                      icPp, 
                                      maxPayloadSize,
				      "enc-mpeg4-generic");
      ismacrypEndSession(icSID);
    } else {
      fprintf(stderr, 
	      "%s: can't hint track type %s\n", ProgName, trackType);
    }
  }
  else if (!strcmp(trackType, MP4_AUDIO_TRACK_TYPE)) {
    const char *media_data_name;
    media_data_name = MP4GetTrackMediaDataName(mp4File, mediaTrackId);
    
    if (strcasecmp(media_data_name, "mp4a") == 0) {
      u_int8_t audioType = MP4GetTrackEsdsObjectTypeId(mp4File, mediaTrackId);

      switch (audioType) {
      case MP4_INVALID_AUDIO_TYPE:
      case MP4_MPEG4_AUDIO_TYPE:
	if (payloadName && 
	    (strcasecmp(payloadName, "latm") == 0 ||
	     strcasecmp(payloadName, "mp4a-latm") == 0)) {
	  rc = MP4AV_Rfc3016LatmHinter(mp4File, mediaTrackId, 
				       maxPayloadSize);
	  break;
	}
      case MP4_MPEG2_AAC_MAIN_AUDIO_TYPE:
      case MP4_MPEG2_AAC_LC_AUDIO_TYPE:
      case MP4_MPEG2_AAC_SSR_AUDIO_TYPE:
	rc = MP4AV_RfcIsmaHinter(mp4File, mediaTrackId, 
				 interleave, maxPayloadSize);
	break;
      case MP4_MPEG1_AUDIO_TYPE:
      case MP4_MPEG2_AUDIO_TYPE:
	if (payloadName && 
	    (!strcasecmp(payloadName, "3119") 
	     || !strcasecmp(payloadName, "mpa-robust"))) {
	  rc = MP4AV_Rfc3119Hinter(mp4File, mediaTrackId, 
				   interleave, maxPayloadSize);
	} else {
	  rc = MP4AV_Rfc2250Hinter(mp4File, mediaTrackId, 
				   false, maxPayloadSize);
	}
	break;
      case MP4_PCM16_BIG_ENDIAN_AUDIO_TYPE:
      case MP4_PCM16_LITTLE_ENDIAN_AUDIO_TYPE:
	rc = L16Hinter(mp4File, mediaTrackId, maxPayloadSize);
	break;
      default:
	fprintf(stderr, 
		"%s: can't hint non-MPEG4/non-MP3 audio type\n", ProgName);
      }
    } else if (strcasecmp(media_data_name, "samr") == 0 ||
	       strcasecmp(media_data_name, "sawb") == 0) {
      rc = MP4AV_Rfc3267Hinter(mp4File, mediaTrackId, maxPayloadSize);
    }
  } else if (!strcmp(trackType, MP4_VIDEO_TRACK_TYPE)) {
    const char *media_data_name;
    media_data_name = MP4GetTrackMediaDataName(mp4File, mediaTrackId);
    
    if (strcasecmp(media_data_name, "mp4v") == 0) {
      u_int8_t videoType = MP4GetTrackEsdsObjectTypeId(mp4File, mediaTrackId);
      
      switch (videoType) {
      case MP4_MPEG4_VIDEO_TYPE:
	rc = MP4AV_Rfc3016Hinter(mp4File, mediaTrackId, maxPayloadSize);
	break;
      case MP4_MPEG1_VIDEO_TYPE:
      case MP4_MPEG2_SIMPLE_VIDEO_TYPE:
      case MP4_MPEG2_MAIN_VIDEO_TYPE:
	rc = Mpeg12Hinter(mp4File, mediaTrackId, maxPayloadSize);
	break;
      default:
	fprintf(stderr, 
		"%s: can't hint non-MPEG4 video type\n", ProgName);
	break;
      }
    } else if (strcasecmp(media_data_name, "avc1") == 0) {
      // h264;
      rc = MP4AV_H264Hinter(mp4File, mediaTrackId, maxPayloadSize);
    } else if (strcasecmp(media_data_name, "s263") == 0) {
      rc = MP4AV_Rfc2429Hinter(mp4File, mediaTrackId, maxPayloadSize);
    }
  } else {
    fprintf(stderr, 
	    "%s: can't hint track type %s\n", ProgName, trackType);
  }

  if (!rc) {
    fprintf(stderr, 
	    "%s: error hinting track %u\n", ProgName, mediaTrackId);
    MP4Close(mp4File);
    exit(EXIT_CREATE_HINT);
  }
}
#endif

#ifndef i6net
/*! \brief Helper function used to find the speech structure attached to a channel */
static struct ast_speech *find_speech(struct ast_channel *chan)
{
    struct ast_speech *speech = NULL;
    struct ast_datastore *datastore = NULL;

    AST_LIST_TRAVERSE_SAFE_BEGIN(&chan->datastores, datastore, entry)
    {
        if (!strcmp(datastore->info->type, "speech"))
        {
            break;
        }
    }
    AST_LIST_TRAVERSE_SAFE_END
        //datastore = ast_channel_datastore_find(chan, &speech_datastore, NULL);
        if (datastore == NULL)
    {
        return NULL;
    }
    speech = datastore->data;

    return speech;
}
#endif

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


static int mp4_rtp_write_audio(struct mp4track *t, struct ast_frame *f, int payload)
{
	/* Next sample */
	t->sampleId++;

	if (option_debug > 5) ast_log(LOG_DEBUG, "Saving #%d:%d:%d %d samples %d size of audio, payload=%d\n", t->sampleId, t->track, t->hint, f->samples, f->datalen,payload);
	/* dump_buffer_hex("AMR buffer",f->data,f->datalen); */

	/* Add hint */
	MP4AddRtpHint(t->mp4, t->hint);

 if (t->hint != -1)
 {
	/* Add rtp packet to hint track */
	MP4AddRtpPacket(t->mp4, t->hint, 0, 0);

	/* Save rtp specific payload header to hint */
	if (payload > 0)
		MP4AddRtpImmediateData(t->mp4, t->hint, AST_FRAME_GET_BUFFER(f), payload);

	/* Set which part of sample audio goes to this rtp packet */
	MP4AddRtpSampleData(t->mp4, t->hint, t->sampleId, 0, f->datalen - payload);

	/* Write rtp hint */
	MP4WriteRtpHint(t->mp4, t->hint, f->samples, 1);
	}

	/* Write audio */
	MP4WriteSample(t->mp4, t->track, AST_FRAME_GET_BUFFER(f) + payload, f->datalen - payload, f->samples, 0, 0);

	return 0;
}

static unsigned char silence_alaw[] = {
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5,
 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5,
  
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5,
  
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 0xF5, 
};
	
static unsigned char silence_ulaw[] = {
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static unsigned char silence_amr[] =
{
0x70,
0x3C, 0x48, 0xF5, 0x1F, 0x96, 0x66, 0x78, 0x00,
0x00, 0x01, 0xE7, 0x8A, 0x00, 0x00, 0x00, 0x00,
0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
#if 0
static unsigned char silence_amr2[] =
{
0x7C,
};
#endif
static int mp4_rtp_write_audio_silence(struct mp4track *t, int payload, struct ast_frame *f)
{	
    /* Unset */
    memset(f, 0, PKT_SIZE);

    /* Let mp4 lib allocate memory */
    AST_FRAME_SET_BUFFER(f,f,PKT_OFFSET,PKT_PAYLOAD);

    /* Don't free the frame outside */
    f->mallocd = 0;
	
    switch(payload)
    {
	case 0:
	    memcpy(AST_FRAME_GET_BUFFER(f), silence_ulaw, sizeof(silence_ulaw));
	    f->datalen = 160;
	    f->samples = 160;
	    mp4_rtp_write_audio(t, f, 0);
	    break;

	case 8:
	    memcpy(AST_FRAME_GET_BUFFER(f), silence_alaw, sizeof(silence_alaw));
	    f->datalen = 160;
	    f->samples = 160;
	    mp4_rtp_write_audio(t, f, 0);
	    break;

	case 32:
	    memcpy(AST_FRAME_GET_BUFFER(f), silence_amr, sizeof(silence_amr));
	    f->datalen = 33;
	    f->samples = 160;
	    mp4_rtp_write_audio(t, f, 1);
	    break;

	default:
	    ast_log(LOG_NOTICE, "Silence generation not supported for payload %d.\n",
		    payload);
	    break;
    }  
    return 0;
}


/* Extract payload according to RFC 2198 */
#define MAX_REDUNDANCY_LEVEL 4
#define min(X, Y)  ((X) < (Y) ? (X) : (Y))

static struct ast_frame *decode_redundant_payload( const struct ast_frame * f, struct ast_frame * decoded[] )
{
    const unsigned char *pt = (const unsigned char *) f->data;
    size_t srclen = f->datalen;
    int idx = 0, maxidx = 0;
    int i = 0;
    /* read block headers */
    while ( i < srclen )
    {
	int ptype = pt[i];
	int len, t_offset;

	if (ptype & 0x80)
	{
	    len  = (( pt[i+2] & 0x03) << 8) + pt[i+3];
	    t_offset = (pt[i+1] << 8) + (pt[i+2] & 0xFC);
	    t_offset  = t_offset >> 2;

	    if (option_debug > 2)
		ast_log(LOG_DEBUG, "Found red block at offset %d (total length = %ld) with payload type %02x timestamp offset %d len %d.\n",
			i, srclen, ptype & 0x7F, t_offset, len );
	    i+=4;
	}
	else
	{
	    len = 1500; /* magic value */
	    t_offset = 0;

           if (option_debug > 2)
                ast_log(LOG_DEBUG, "Found primary block at offset %d with payload type %02x.\n",
                        i, ptype );
	   i++;
	}

	if (idx >= MAX_REDUNDANCY_LEVEL)
	{
	    ast_log(LOG_WARNING, "decode_redundant_payload: Too many blocks of redundant data in this frame.\n");
	    goto dec_nomem;
        }

	struct ast_frame * df = (struct ast_frame *) malloc(sizeof(struct ast_frame) + len + AST_FRIENDLY_OFFSET + 1 );
	
	if (df == NULL)
	{
	    ast_log(LOG_WARNING, "decode_redundant_payload: failed to allocated memory for decoded frames.\n");
	    goto dec_nomem;
        }

	/* hardcoed sample frequency for text is 1000 Hz */
	/* true formula : tv_usec = ( t_offset * 10000000 / freq ) mod 1 000 000 */
	df->delivery.tv_usec = (t_offset * 1000)  % 1000000;
        df->delivery.tv_sec = t_offset / 1000;
	df->mallocd = AST_MALLOCD_HDR; /* indicate that memory must be freed */
	df->frametype = AST_FRAME_TEXT; /* hardcoded */
	df->subclass = AST_FORMAT_T140; /* hardcoded but could be deducted from pt */
	df->data = ((void *)df) + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET; 
        df->offset = sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
        df->datalen = len;

	decoded[idx++] = df;
	if ( (ptype & 0x80)  == 0 ) break;
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Now processing following block #%d.\n", idx);
    }
	
    maxidx = idx;

    /* Fill remaining frame slots with null pointers */
    while (idx < MAX_REDUNDANCY_LEVEL)
	decoded[idx++] = NULL;

    /* now store datablocs in allocated frames */
    for(idx = 0; idx < maxidx ; idx++) 
    {
	int  len;
	struct ast_frame *df = decoded[idx];
	

	if (df == NULL)
	{
	    ast_log(LOG_WARNING, "Unexpedcted NULL frame for block #%d.\n", idx);
	    break;
	}

	len = min( df->datalen, srclen - i );
        if ( i + len > srclen )
        {
	    ast_log(LOG_WARNING, "Block #%d has a length out of bound.\n", idx);
	    df->datalen = 0;
            /* Out of bound */
            break;
        }

	if (option_debug > 2)
		ast_log(LOG_DEBUG, "block #%d contains %d bytes.\n", idx, len);
	if (len > 0)
	{
	    memcpy(df->data, &pt[i], len);
	    i += len;
	}
	df->datalen = len;

    }
    if ( maxidx > 0) 
	return decoded[maxidx - 1];
    else
	return NULL;

dec_nomem:
    maxidx = idx;
    for (idx = 0; idx < maxidx; idx++)
    {
	if (decoded[idx]) ast_frfree( decoded[idx] );
    }

    return NULL;
}

static void mp4_rtp_write_video_frame(struct mp4track *t, int samples)
{
    if (option_debug > 5) ast_log(LOG_DEBUG, "Saving #%d:%d:%d %d samples %d size of video\n", t->sampleId, t->track, t->hint, samples, t->length);

	/* Save rtp hint */
	if (t->hint != -1)
	MP4WriteRtpHint(t->mp4, t->hint, samples, t->intra);
	/* Save video frame */
	MP4WriteSample(t->mp4, t->track, t->frame, t->length, samples, 0, t->intra);
}

static int mp4_rtp_write_video(struct mp4track *t, struct ast_frame *f, int payload, bool intra, int skip, unsigned char * prependBuffer, int prependLength)
{
	/* rtp mark */
	const bool mBit = f->subclass & 0x1;
	
	/* If it's the first packet of a new frame */
	if (t->first)
 {
		/* If we hava a sample */
		if (t->sampleId > 0) {
		 
			/* Save frame */
			//mp4_rtp_write_video_frame(t, f->samples / (90000 / t->timeScale)); //!!!!!
			mp4_rtp_write_video_frame(t, f->samples); //!!!!!
		}

		t->length = 0;	

		/* Save intra flag */
		t->intra = intra;

		if (option_debug > 4) ast_log(LOG_DEBUG, "New video hint [%d,%d,%d,%d]\n",intra,payload,skip,prependLength);

		/* Add hint */
		if (t->hint != -1)
		MP4AddRtpHint(t->mp4, t->hint);

		/* Next frame */
		if (t->first)
		t->sampleId++;

		/* Reset first mark */
		t->first = 0;
	}

	/* Add rtp packet to hint track */
	if (t->hint != -1)
	MP4AddRtpPacket(t->mp4, t->hint, mBit, 0);

	/* Save rtp specific payload header to hint */
	if (t->hint != -1)
	if (payload > 0)
		MP4AddRtpImmediateData(t->mp4, t->hint, AST_FRAME_GET_BUFFER(f), payload);

  if (  f->subclass  & AST_FORMAT_H263)
  {
    /* Set hint reference to video data */
		if (option_debug > 4) 
      ast_log(LOG_DEBUG, "H263 intra[%d] payload[%d] skip[%d] prependLength[%d]\n",
              intra,payload,skip,prependLength);

    /* Depacketize */
		t->length += rfc2190_append( t->frame,t->length,AST_FRAME_GET_BUFFER(f),f->datalen);
  }
  else
  {
    /* If we have to prepend */
    if (prependLength<0)
    {
      /* Prepend data to video buffer */
      memcpy(t->frame, (char *)prependBuffer, -prependLength);

      /* Inc length */
      if (t->length == 0)
        t->length += -prependLength;
    }
    else if (prependLength)
    {
      /* Prepend data to video buffer */
      memcpy(t->frame + t->length, (char*)prependBuffer, prependLength);

      /* Inc length */
      t->length += prependLength;
    }

    /* Set hint reference to video data */
    if (t->hint != -1)
      MP4AddRtpSampleData(t->mp4, t->hint, t->sampleId, (u_int32_t) t->length, f->datalen - payload - skip);

    /* Copy the video data to buffer */
    memcpy(t->frame + t->length , AST_FRAME_GET_BUFFER(f) + payload + skip, f->datalen - payload - skip); 

    /* Increase stored buffer length */
    t->length += f->datalen - payload - skip;	
  }
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
	int frameTime;
	int frameType;
	int frameSubClass;
	char *name;
	char *src;
	unsigned char type;

};



static int mp4_video_read(struct mp4rtp *p)
{
	int          next = 0;
	int          last = 0;
	int          first = 1;
	u_int8_t*    data  = NULL  ;
  // MP4Timestamp StartTime ;
  MP4Duration  Duration ; 
  // MP4Duration  RenderingOffset;
  // bool         IsSyncSample = false ;
  uint32_t     NumBytes = 0;
  uint32_t     len      = 0;
  uint32_t     sent     = 0;

  double       fps      = MP4GetTrackVideoFrameRate(p->mp4 , p->track );
  if (  !MP4ReadSample(p->mp4, 
                       p->track, 
                       p->sampleId++,
                       &data,
                       &NumBytes,
                       0,
                       &Duration,
                       0,
                       0) ) 
  {
     if ( option_debug > 1 )
       ast_log(LOG_ERROR, "Error reading H263 packet [%d]\n", p->track);
    return -1;
  }

 

  Duration  = Duration / 90 ;
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG, "MP4ReadSample Duration[%d] lenght[%d] @%d \n",(int)Duration,NumBytes,(int)fps );

  while(sent<NumBytes)
  {
    if (sent+H263_FRAME_SIZE>NumBytes)
    {
      last = 1;
      len = NumBytes-sent;
    } else 
      len = H263_FRAME_SIZE;

    SendVideoFrameH263(p->chan, &data[sent], len, first, last,fps);
    first = 0;
    sent += len;
  }

  free(data);
  
  next = (Duration)?(int)Duration:(int)(900/fps);

	if (option_debug > 4)
		ast_log(LOG_DEBUG, "mp4_video_read return [%d]\n", next);

	/* exit next send time */
	return next;
}

static int mp4_rtp_read(struct mp4rtp *p, struct ast_frame *f)
{
	//unsigned char buffer[PKT_SIZE];

//#define BUFFERLEN (sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + 1500)
	//unsigned char buffer[BUFFERLEN + 1];
	//struct ast_frame *f = (struct ast_frame *) buffer;
  	
	int next = 0;
	int last = 0;
	int first = 0;
	uint8_t* data;
	
	/* If it's first packet of a frame */
	if (!p->numHintSamples) {
		/* Get number of rtp packets for this sample */
		if (!MP4ReadRtpHint(p->mp4, p->hint, p->sampleId, &p->numHintSamples)) {
			ast_log(LOG_DEBUG, "MP4ReadRtpHint failed [%d,%d]\n", p->hint,p->sampleId);
			return -1;
		}

		/* Get number of samples for this sample */
		p->frameSamples = MP4GetSampleDuration(p->mp4, p->hint, p->sampleId);

		/* Get size of sample */
		p->frameSize = MP4GetSampleSize(p->mp4, p->hint, p->sampleId);

		/* Get sample timestamp */
		p->frameTime = MP4GetSampleTime(p->mp4, p->hint, p->sampleId);

		/* Set first flag */
		first = 1;
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
			f->samples = p->frameSamples * (90000 / p->timeScale);
	} else {
		/* Set number of samples */
		f->samples = p->frameSamples;
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
		ast_log(LOG_ERROR, "Error reading packet [%d,%d]\n", p->hint, p->track);
		return -1;
	}

	if (option_debug > 6)
		ast_log(LOG_DEBUG, "MP4ReadRtpHint samples/lenght [%d,%d]\n", f->samples, f->datalen);

	/* Write frame */
	ast_write(p->chan, f);

	/* Are we the last packet in a hint? */
	if (last) {
		/* The first hint */
		p->packetIndex = 0;
		/* Go for next sample */
		p->sampleId++;
		p->numHintSamples = 0;
	}

	/* Set next send time */
	if ((!last) && (f->frametype == AST_FRAME_VIDEO))
		/* Send next now if it's not the last packet of the frame */
		/* This will send all the packets from the same frame without pausing between them */
		/* FIX: should wait depending on bandwith */
		next = 0;
	else if (p->timeScale)
		/* If it's from a different frame or it's audio */
		next = (p->frameSamples * 1000) / p->timeScale;
	else
		next = -1;

	if (option_debug > 5)
		ast_log(LOG_DEBUG, "MP4ReadRtpHint return [%d]\n", next);

	/* exit next send time */
	return next;
}

static int mp4_play(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	struct mp4rtp audio = { chan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0 };
	struct mp4rtp video = { chan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0 };
	MP4FileHandle mp4;
	MP4TrackId hintId = NO_CODEC ;
	MP4TrackId trackId;
	const char *type = NULL;
	int audioNext = -1;
	int videoNext = -1;
	int totalAudio = 0;
	int totalVideo = 0;
	int total = 0;
	int t = 0;
	int idxTrack = 0;
	struct ast_frame *f = NULL;
	char src[128];
	int res = 0;
	struct timeval tv ;
	struct timeval tvn ;
	struct timeval tvs ;

	char *parse;
	int numberofDigits = -1;
	char *varName = NULL;
	char *stopChars = NULL;
	char dtmfBuffer[MAX_DTMF_BUFFER_SIZE];
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	int autohint = 0;
	
	char *name;
  int audioBestId = NO_CODEC;
  int audioULAWId = NO_CODEC;  
  int audioLastId = NO_CODEC;  
  int videoBestId = NO_CODEC;

#ifdef VIDEOCAPS
  int VideoNativeID[NATIVE_VIDEO_CODEC_LAST] ;
  int h263Play   = 0 ;
#else
  int videoLastId = NO_CODEC;  
#endif

	unsigned char buffer[PKT_SIZE];
	unsigned char buffer2[PKT_SIZE];
	//struct ast_frame *f = (struct ast_frame *) buffer;
	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(filename); AST_APP_ARG(options););

#ifdef VIDEOCAPS
  memset( VideoNativeID , NO_CODEC , NATIVE_VIDEO_CODEC_LAST*sizeof(int) ) ;
#endif

#ifdef i6net_lock	
  if (vxml_lock(chan))
  {
    ast_log(LOG_WARNING, "Lock error !\n");
    return -1;
  }
#endif

#ifndef i6net
 struct ast_speech *speech = find_speech(chan);
 int laststate = 0xFF; 
 int lastflags = 0xFF; 
 char cformat1[_STR_CODEC_SIZE] = {0};
 char cformat2[_STR_CODEC_SIZE] = {0};
 if (speech != NULL)
 ast_log(LOG_WARNING, "mp4_play:	Found speech enabled!\n");
#endif	

	/* Check for data */
	if (!data || ast_strlen_zero(data)) {
                ast_log(LOG_WARNING, "mp4play requires an argument (filename)\n");
                return -1;
        }

	ast_log(LOG_DEBUG, "mp4play %s\n",(char *)data );

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
		if (numberofDigits<0) {
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
	if (autohint)
	mp4 = MP4Modify((char *) args.filename, 9, 0);
	else
	mp4 = MP4Read((char *) args.filename, 9);
	
	/* If not valid */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
		ast_log(LOG_WARNING, "mp4play invalide file.\n");
		/* exit */
		res = -1;
		goto clean;
	}

	/* Disable Verbosity */
	MP4SetVerbosity(mp4, 5);
	idxTrack=0;

	ast_log(LOG_DEBUG, "Native formats:%s , Chann capability ( videocaps.cap ):%s\n", 
          ast_getformatname_multiple(cformat1,_STR_CODEC_SIZE, chan->nativeformats),
          ast_getformatname_multiple(cformat2,_STR_CODEC_SIZE, chan->channelcaps.cap));

	/* Get the first hint track */
  ast_log(LOG_NOTICE, "Find first track %d\n",idxTrack);  
	hintId = MP4FindTrackId(mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0);

	/* Iterate hint tracks */
	while (hintId != MP4_INVALID_TRACK_ID) {
    const char* nm = MP4GetTrackMediaDataName(mp4,hintId) ;
		ast_log(LOG_NOTICE, "found track %d (%s)\n", hintId,nm?nm:"null");

		/* Get asociated track */
		trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
		
		/* Check it's good */
		if (trackId != MP4_INVALID_TRACK_ID) {

			/* Get type */
			type = MP4GetTrackType(mp4, trackId);

			if (type != NULL)
			ast_log(LOG_NOTICE, "track #[%d] type[%s] :", trackId, type);
			else
			ast_log(LOG_NOTICE, "track %d (null)\n", trackId);

			/* Check track type */
			if (type != NULL)
			{
			 if (strcmp(type, MP4_AUDIO_TRACK_TYPE) == 0) {

     		/* Get audio type */
		    MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);

        if (name)
 			  ast_log(LOG_NOTICE, "audio track codec[%s]\n", name);
 			  else
 			  ast_log(LOG_NOTICE, "audio track (null)\n");

				/* Depending on the name */
				if (name == NULL) // Vidiator
        {
					audioLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_AMRNB)
					audioBestId = hintId;
        }
        else
        if (strcmp("PCMU", name) == 0)
				{
					audioLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_ULAW)
					audioBestId = hintId;
					audioULAWId = hintId;
				}
				else
        if (strcmp("PCMA", name) == 0)
				{
					audioLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_ALAW)
					audioBestId = hintId;
				} 
				else
        if (strcmp("AMR", name) == 0)
				{
					audioLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_AMRNB)
					audioBestId = hintId;
				}			
			 } 
       else if (strcmp(type, MP4_VIDEO_TRACK_TYPE) == 0) 
       {
				 /* Get video type */
			 	 MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);

         if (name)
 			   ast_log(LOG_NOTICE, "video track codec[%s]\n", name);
 			   else
 			   ast_log(LOG_NOTICE, "video track (null)\n");


				 /* Depending on the name */
#ifdef VIDEOCAPS
         if (  (chan->channelcaps.cap & AST_FORMAT_H263_PLUS ) &&
                    ((strcmp("H263-1998", name)==0)  ||
                     (strcmp("H263-2000", name)==0)  ))
         {
            videoBestId = hintId;
            ast_log(LOG_NOTICE, "Select H263P videoBestId = %d\n",hintId);
         }
				 else if ( (chan->channelcaps.cap & AST_FORMAT_H264) &&
                   ( strcmp("H264", name) == 0))
				 {
           videoBestId = hintId;
           ast_log(LOG_NOTICE, "Select H264 videoBestId = %d\n",hintId);
         }
         else if ((chan->nativeformats & AST_FORMAT_H263_PLUS) &&
                  (( strcmp("H263-1998", name) == 0 ) ||
                   ( strcmp("H263-2000", name) == 0 ) ))
				 {
           VideoNativeID[NATIVE_VIDEO_CODEC_H263P] = hintId;
           ast_log(LOG_NOTICE, "VideoNativeID H263P = %d\n",hintId);
         }
 				 else if ((chan->nativeformats & AST_FORMAT_H264 ) &&
                  (strcmp("H264", name) == 0) )
         {
           VideoNativeID[NATIVE_VIDEO_CODEC_H264] = hintId;
            ast_log(LOG_NOTICE, "VideoNativeID H264 = %d\n",hintId);
         }
				 else if ( name == NULL )
				 {
           if ((chan->channelcaps.cap & AST_FORMAT_H263_PLUS ) ||
               (chan->nativeformats & AST_FORMAT_H263_PLUS))
           {
             VideoNativeID[NATIVE_VIDEO_CODEC_H263P] = hintId;
            ast_log(LOG_NOTICE, "VideoNativeID H263P = %d\n",hintId);

           }
         }
#else
				 if (name == NULL) // Vidiator
         {
					videoLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_H263_PLUS)
					videoBestId = hintId;
         }
         else
				 if (strcmp("H263", name) == 0)
				 {
					videoLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_H263)
					videoBestId = hintId;
         }
				 else if (strcmp("H263-1998", name) == 0)
				 {
					videoLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_H263_PLUS)
					videoBestId = hintId;
         }
				 else if (strcmp("H263-2000", name) == 0)
				 {
					videoLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_H263_PLUS)
					videoBestId = hintId;
         }
				 else if (strcmp("H264", name) == 0)
				 {
					videoLastId = hintId;
					if (chan->nativeformats & AST_FORMAT_H264)
					videoBestId = hintId;
         }
#endif
			 }
			}
		}

		/* Get the next hint track */
    idxTrack++;
    ast_log(LOG_NOTICE, "Find next track %d\n",idxTrack);
		hintId = MP4FindTrackId(mp4, idxTrack , MP4_HINT_TRACK_TYPE, 0);
	}

	/* If not valid */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
		/* exit */
	  ast_log(LOG_WARNING, "mp4_play:	Invalid file : %s!\n", args.filename);
		res = -1;
		goto clean;
	}

  /* Choose best Audio hintId */
  if (audioBestId != NO_CODEC)
    hintId = audioBestId;
  else
    if (audioULAWId != NO_CODEC)
      hintId = audioULAWId;
    else
      hintId = audioLastId;
  
  /* No audio and no video */
  if ( hintId == NO_CODEC  )
  {
		/* exit */
	  ast_log(LOG_WARNING, "mp4_play: no audio %s!\n", args.filename );
		//res = -1;	
    //goto clean;
  }
      
  if (hintId != NO_CODEC)
  { 
     int oldnative;
	/* Get asociated track */
	trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
	
	audio.mp4 = mp4;
	audio.hint = hintId;
	audio.track = trackId;
	audio.sampleId = 1;
	audio.packetIndex = 0;
	audio.frameType = AST_FRAME_VOICE;

	/* Get audio type */
	MP4GetHintTrackRtpPayload(mp4, hintId, &audio.name, &audio.type, NULL, NULL);

    if (audio.name)
        ast_log(LOG_NOTICE, "set audio track %d %s\n", hintId, audio.name);

    /* Get time scale */
    audio.timeScale = MP4GetTrackTimeScale(mp4, hintId);	
    
		/* Depending on the name */
    if (audio.name == NULL)
	audio.frameSubClass = AST_FORMAT_AMRNB;     
    else if (strcmp("PCMU", audio.name) == 0)
	audio.frameSubClass = AST_FORMAT_ULAW;
    else if (strcmp("PCMA", audio.name) == 0)
	audio.frameSubClass = AST_FORMAT_ALAW;
    else if (strcmp("AMR", audio.name) == 0)
	audio.frameSubClass = AST_FORMAT_AMRNB;

#ifdef VIDEOCAPS
   oldnative = chan->nativeformats;

   if ( chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK )
   {
	chan->nativeformats =  chan->channelcaps.cap;
	ast_log(LOG_WARNING, "mp4_play:	already received audio format %08x.\n",
		chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK)	;
   }
   else
   {
	ast_log(LOG_WARNING, "mp4_play: using original native formats.\n");
   }
	
#endif

    if ( ast_set_write_format(chan, audio.frameSubClass) )
	  ast_log(LOG_WARNING, "mp4_play:	Unable to set write format to %s!\n", audio.name);

#ifdef VIDEOCAPS
    chan->nativeformats = oldnative;
#endif
  }
  else
  {
	  audio.mp4 = MP4_INVALID_FILE_HANDLE;
	  audio.hint = MP4_INVALID_TRACK_ID;
  }

  ast_log(LOG_NOTICE, "Choice videoBestId %d \n", videoBestId);
  /* Choose best Video hintId */
  if (videoBestId != NO_CODEC)
  {
    hintId = videoBestId;
    ast_log(LOG_WARNING, "mp4_play: Track selected by prefered codec 0x%X track[0x%X] \n",
            chan->channelcaps.cap ,hintId );
  }
  else
#ifdef VIDEOCAPS
  {
    ast_log(LOG_DEBUG, "mp4_play: find in native codec \n ");
    int idx = 0 ;
    hintId = NO_CODEC ;
    while ( idx < NATIVE_VIDEO_CODEC_LAST && hintId == NO_CODEC )
    {
      ast_log(LOG_WARNING, "mp4_play: Native codec  VideoNativeID[%d]=%d\n",
              idx,VideoNativeID[idx]);
      if ( VideoNativeID[idx] != NO_CODEC )
      {
        hintId = VideoNativeID[idx] ;
        ast_log(LOG_WARNING, "mp4_play: Track selected by native codec track[0x%X] \n",
         hintId  );
      }
      idx ++ ;
    }

    if ( hintId == NO_CODEC && (chan->nativeformats & AST_FORMAT_H263 )) 
    {
      uint32_t numTracks = MP4GetNumberOfTracks(mp4,0,0);
      uint32_t i = 0 ;
      for ( i=0 ; i < numTracks; i++) 
      {
        MP4TrackId tId = MP4FindTrackId(mp4, i,0,0);
        const char* trackType = MP4GetTrackType(mp4, tId);
        if (!strcmp(trackType, MP4_VIDEO_TRACK_TYPE)) {
          const char *media_data_name = MP4GetTrackMediaDataName(mp4, tId);
           if (strcasecmp(media_data_name, "s263") == 0) {
             ast_log(LOG_WARNING, "mp4_play: Special case of H263 using track %d \n",i);
             trackId = tId ; 
             h263Play = 1 ;
             i = numTracks ;
           }
        }
      }
    }
  }
#else
  {
    hintId = NO_CODEC;
  }
#endif
  
  if (hintId != NO_CODEC)
  {
		/* Get asociated track */
		trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);

		/* it's video */
		video.mp4 = mp4;
		video.hint = hintId;
		video.track = trackId;
		video.sampleId = 1;
		video.packetIndex = 0;
		video.frameType = AST_FRAME_VIDEO;  
      
 	  /* Get video type */
 	  MP4GetHintTrackRtpPayload(mp4, hintId, &video.name, &video.type, NULL, NULL);

    if (video.name)
      ast_log(LOG_NOTICE, "set video track %d %s\n", hintId, video.name);
  
	  /* Get time scale */
		video.timeScale = MP4GetTrackTimeScale(mp4, hintId);

		/* Depending on the name */
		if (video.name == NULL) // Vidiator
		video.frameSubClass = AST_FORMAT_H263_PLUS;
		else if (strcmp("H263", video.name) == 0)
 		video.frameSubClass = AST_FORMAT_H263;
		else if (strcmp("H263-1998", video.name) == 0)
		video.frameSubClass = AST_FORMAT_H263_PLUS;
		else if (strcmp("H263-2000", video.name) == 0)
		video.frameSubClass = AST_FORMAT_H263_PLUS;
		else if (strcmp("H264", video.name) == 0)
		video.frameSubClass = AST_FORMAT_H264;
  }
	else
	{
    if ( h263Play )
    {
      video.mp4 = mp4;
      video.hint = hintId;
      video.track = trackId;
      video.sampleId = 1;
      video.packetIndex = 0;
      video.frameType = AST_FRAME_VIDEO;  
      video.name=(char*)h263VideoName;
      video.frameSubClass = AST_FORMAT_H263;
      if (video.name)
        ast_log(LOG_NOTICE, "set video track %d %s\n", trackId, video.name);
    }
    else
    {
      video.mp4 = MP4_INVALID_FILE_HANDLE;
      video.hint = MP4_INVALID_TRACK_ID;
    }
  }
  
	
#ifndef i6net	
 idxTrack = 0;
 
 if (autohint)
 if (!video.name)
 {
   const char *media_data_name;
   int rc;
   
   ast_log(LOG_DEBUG, "Autohint the video track\n");

	  /* Get the first hint track */
    ast_log(LOG_NOTICE, "Find first track %d\n",idxTrack);   
	  trackId = MP4FindTrackId(mp4,idxTrack, MP4_VIDEO_TRACK_TYPE, 0);

	  /* Iterate hint tracks */
	  while (trackId != MP4_INVALID_TRACK_ID) {
			  /* Get type */
			  type = MP4GetTrackType(mp4, trackId);
			
			  if (type != NULL)
     ast_log(LOG_DEBUG, "Found track %s !\n", type);
     
     media_data_name = MP4GetTrackMediaDataName(mp4, trackId);

			  if (media_data_name != NULL)
     ast_log(LOG_DEBUG, "Found track %s !\n", media_data_name);
     
     if (strcasecmp(media_data_name, "avc1") == 0) {
      // h264;
      rc = MP4AV_H264Hinter(mp4, trackId, 1460);
     }
      
     if (strcasecmp(media_data_name, "s263") == 0) {
      // h263;
      rc = MP4AV_Rfc2429Hinter(mp4, trackId, 1460);
     }     
          
     /* Get the next hint track */
     idxTrack++;
     ast_log(LOG_NOTICE, "Find next track %d\n",idxTrack);
     trackId = MP4FindTrackId(mp4, idxTrack, MP4_VIDEO_TRACK_TYPE, 0);
   }
   MP4Close(mp4);     
 }
#endif

 /* Get H264 headers */
 if (video.name)
 if (strcmp("H264", video.name) == 0)
 {
  uint8_t **seqheader, **pictheader;
  uint32_t *pictheadersize, *seqheadersize;
  uint32_t ix;
  //uint8_t header[4] = {0, 0, 0, 1};
  
	 //unsigned char buffer[sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + 1500];
	 //struct ast_frame *f = (struct ast_frame *) buffer;
   
   f = (struct ast_frame *) buffer;  

	 /* Unset */
	 memset(f, 0, sizeof(struct ast_frame) + 1500 + AST_FRIENDLY_OFFSET);

	 /* Let mp4 lib allocate memory */
	 f->data = (void*)f + AST_FRIENDLY_OFFSET;
	 f->datalen = 1500;
	 f->src = strdup(video.src);

	 /* Set type */
	 f->frametype = video.frameType;
	 f->subclass = video.frameSubClass;

   f->delivery.tv_usec = 0;
   f->delivery.tv_sec = 0;
   /* Don't free the frame outside */
   f->mallocd = 0;

   f->samples = 0;
 	 
  
   MP4GetTrackH264SeqPictHeaders(mp4, video.track, 
                                 &seqheader, &seqheadersize,
                                 &pictheader, &pictheadersize);
   for (ix = 0; seqheadersize[ix] != 0; ix++) {
     //dump_buffer_hex("SeqHeader", seqheader[ix], seqheadersize[ix]);
 
    memcpy(f->data, seqheader[ix], seqheadersize[ix]);
				f->datalen = seqheadersize[ix];
			   
	   /* Write frame */
	   {
	     if (option_debug > 5)
		ast_log(LOG_DEBUG, "Play -> chan %d samples %d size of audio, type=%d, subclass=%d\n", f->samples, f->datalen, f->frametype, f->subclass);
	     //dump_buffer_hex("Play dump ", f->data, f->datalen);
	   }
	
	   ast_write(video.chan, f);       
  }
  for (ix = 0; pictheadersize[ix] != 0; ix++) {
    //dump_buffer_hex("PictHeader", pictheader[ix], pictheadersize[ix]);

    memcpy(f->data, pictheader[ix], pictheadersize[ix]);
				f->datalen = pictheadersize[ix];

	   /* Write frame */
	   {
		if (option_debug > 5)
	     ast_log(LOG_DEBUG, "Play -> chan %d samples %d size of audio, type=%d, subclass=%d\n", f->samples, f->datalen, f->frametype, f->subclass);
	     //dump_buffer_hex("Play dump ", f->data, f->datalen);
	   }
	
	   ast_write(video.chan, f);       
  }     
 }

  f = (struct ast_frame *) buffer;
	/* If we have audio */
	//if (audio.name)
	if (audio.mp4 != MP4_INVALID_FILE_HANDLE)
	{
		/* Send audio */
		audioNext = mp4_rtp_read(&audio, f);
		if (audioNext > 0)
    totalAudio += audioNext;
  }

  f = (struct ast_frame *) buffer2;
	/* If we have video */
	//if (video.name)
	if (video.mp4 != MP4_INVALID_FILE_HANDLE)
	{
		/* Send video */
		videoNext = h263Play?mp4_video_read(&video):mp4_rtp_read(&video, f);
		if (videoNext > 0)
    totalVideo += videoNext;
  }

	/* Calculate start time */
	tv = ast_tvnow();
	tvs = tv;

	/* Wait control messages or finish of both streams */
	while (!((audioNext < 0) && (videoNext < 0))) {
		/* Get next time */
		if (audioNext < 0)
			t = videoNext;
		else if (videoNext < 0)
			t = audioNext;
		else if (audioNext < videoNext)
			t = audioNext;
		else
			t = videoNext;

		/* Wait time */
		int ms = t;
		
	  if (option_debug > 5)
      ast_log(LOG_DEBUG, "mp4play Time to wait %d\n", ms);		

		/* Read from channel and wait timeout */
		while (ms > 0) {
			/* Wait */
			ms = ast_waitfor(chan, ms);

			/* if we have been hang up */
			if (ms < 0) 
      {
        ast_log(LOG_DEBUG, "mp4play :  hangup detect exit  \n");
				/* exit */
				goto end;
      }
			/* if we have received something on the channel */
			if (ms > 0) {
				/* Read frame */
				f = ast_read(chan);

				/* If failed */
				if (!f) 
        {
          ast_log(LOG_DEBUG, "mp4play : bad frame exiting  \n");
					/* exit */
					goto end;
        }
#ifndef i6net
    if (speech != NULL)
    if (stopChars != NULL)
    if (strchr(stopChars,'S'))
    /* If it's a voice frame */
				if (f->frametype == AST_FRAME_VOICE)		
    {			
     ast_mutex_lock(&speech->lock);
        				 
     if (speech->state == AST_SPEECH_STATE_READY)
     {
      ast_speech_write(speech, f->data, f->datalen);
     }

     if (speech->state != laststate)
     {
       if (speech->state == AST_SPEECH_STATE_NOT_READY)
 		   ast_log(LOG_DEBUG, "Speech not ready : state '%d'\n", speech->state);
 		   else
       if (speech->state == AST_SPEECH_STATE_READY)
 		   ast_log(LOG_DEBUG, "Speech ready : state '%d'\n", speech->state);
 		   else
       if (speech->state == AST_SPEECH_STATE_WAIT)
 		   ast_log(LOG_DEBUG, "Speech wait : state '%d'\n", speech->state);
 		   else
       if (speech->state == AST_SPEECH_STATE_DONE)
 		   ast_log(LOG_DEBUG, "Speech done : state '%d'\n", speech->state);
 		   else
 		   ast_log(LOG_DEBUG, "Speech ? : state '%d'\n", speech->state);
 		   
		   laststate = speech->state; 
     }
     if (speech->flags != lastflags)
     {
       if (speech->flags & AST_SPEECH_HAVE_RESULTS)
 		   ast_log(LOG_DEBUG, "Speech flags results : flag '%d'\n", speech->flags);
 		   else
       if (speech->flags & AST_SPEECH_QUIET)
 		   ast_log(LOG_DEBUG, "Speech flags quiet : flag '%d'\n", speech->flags);
 		   else
       if (speech->flags & AST_SPEECH_SPOKE)
 		   ast_log(LOG_DEBUG, "Speech flags spoke  : flag '%d'\n", speech->flags);
 		   
		   lastflags = speech->flags; 
     }       
          
     if (speech->state == AST_SPEECH_STATE_DONE)
     {
		  ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);

  		ast_log(LOG_DEBUG, "SPEECH interruption!\n");
      strcpy(dtmfBuffer,"S");

			if (varName)
			/* Build variable */
			pbx_builtin_setvar_helper(chan, varName, dtmfBuffer);

      ast_mutex_unlock(&speech->lock);       
					
      /* Free frame */						
      ast_frfree(f);
	  	/* exit */
			goto end;  
     }		
     
     if (speech->flags & AST_SPEECH_QUIET)
     {
  		ast_log(LOG_DEBUG, "SPEECH interruption!\n");
      strcpy(dtmfBuffer,"s");

			if (varName)
			/* Build variable */
			pbx_builtin_setvar_helper(chan, varName, dtmfBuffer);

      ast_mutex_unlock(&speech->lock);       
					
      /* Free frame */						
      ast_frfree(f);
	  	/* exit */
			goto end;  
     }
     	 
     ast_mutex_unlock(&speech->lock);
    }    
#endif

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

			/* Calculate elapsed time */
			ms = t - ast_tvdiff_ms(ast_tvnow(),tv);
	    if (option_debug > 5)
			ast_log(LOG_DEBUG, "mp4play New time to wait %d\n", ms);					
		}

		/* Get new time */
		tvn = ast_tvnow();

		/* Calculate elapsed */
		t = ast_tvdiff_ms(tvn,tv);
		
	  if (option_debug > 5) 
		ast_log(LOG_DEBUG, "mp4play Delta time %d\n", t);					

		/* Set new time */
		tv = tvn;

		/* Remove time */
		if (audioNext > 0)
			audioNext -= t;
		if (videoNext > 0)
			videoNext -= t;

    f = (struct ast_frame *) buffer;

		/* if we have to send audio */
		//if (audioNext<=0 && audio.name)
		if (audioNext<=0 && (audio.mp4 != MP4_INVALID_FILE_HANDLE))
	  {
	  	/* Send audio */
	  	audioNext = mp4_rtp_read(&audio, f);
	  	if (audioNext > 0)
      totalAudio += audioNext;
    }

    f = (struct ast_frame *) buffer2;
  
		/* or video */
		//if (videoNext<=0 && video.name)
		if (videoNext<=0 && (video.mp4 != MP4_INVALID_FILE_HANDLE))
		{
			videoNext = h263Play?mp4_video_read(&video):mp4_rtp_read(&video, f);
			if (videoNext > 0)
      totalVideo += videoNext;
		}

		total = ast_tvdiff_ms(ast_tvnow(),tvs);

	  if (option_debug > 5)
		ast_log(LOG_DEBUG, "mp4play total %d (audio %d, video %d)\n", total, totalAudio, totalVideo);
	  
    // Shift correction
    if (videoNext > 0)
      if (totalVideo < total)
      {
        videoNext = 0;
        if (option_debug > 5)
          ast_log(LOG_DEBUG, "mp4play Video correction !\n");
      }
		if (audioNext > 0)
      if (totalAudio < total)
      {
        audioNext = 0;
        if (option_debug > 5)
          ast_log(LOG_DEBUG, "mp4play Audio correction !\n");
      }

	}

end:
	/* Log end */
	ast_log(LOG_DEBUG, "<app_mp4\n");

	/* Close file */
	MP4Close(mp4);

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
	MP4TrackId hintAudio = -1;
	MP4TrackId hintVideo = -1;
	unsigned char type = 0;
	char *params = NULL;
	int audio_payload = 0, video_payload = 0;
	int loopVideo = 0;
	int waitVideo = 0;
	int fulength = 0;
	unsigned char h264_buffer[100];
	int maxduration = 0; //1000*10;		/* max duration of recording in milliseconds */
	int waitres;
	int gottimeout=0;
  long oldAudioTs = 0L ; 
	long VideoDuration;
  long AudioDuration;
  long TextDuration;
	long lastdurationVideo;
	long lastdurationAudio;
  long lastdurationText;
  
	unsigned int timestampVideo;
	unsigned int timestampAudio;
	int correctionVideo;
	int synchroVideo = 1;

#ifdef FIX_TIME
	int synchroAudio = 0;
	//unsigned int samplesVideo;	
	int autocorrectionVideo = 0;
	int autocorrectionAudio = 0;
#endif

  int haveAudio           =  chan->nativeformats & AST_FORMAT_AUDIO_MASK ;
  int haveVideo           =  chan->nativeformats & AST_FORMAT_VIDEO_MASK ;  
  int haveText            =  chan->nativeformats & AST_FORMAT_TEXT_MASK ;  
	//Text handling
	int saveText = 1;  // force this param because it is not present in vxml

  char   txtBuff[AST_MAX_TXT_SIZE] = { '\n' } ;
  size_t IdxTxtBuff = 1 ;
	
	unsigned char buffer[PKT_SIZE];
	struct ast_frame *f2 = (struct ast_frame *) buffer;

  // Time ref 
  struct timeval CurrAudioTv = { 0 , 0 };
  struct timeval LastAudioTv = { 0 , 0 };
  struct timeval CurrVideoTv = { 0 , 0 };
  struct timeval LastVideoTv = { 0 , 0 };
  long long      TimeDiff    = 0L;

	struct timeval video_tvn   = { 0 , 0 };
	struct timeval video_tvs   = { 0 , 0 };
	struct timeval text_tvn    = { 0 , 0 };
	struct timeval text_tvs    = { 0 , 0 };
	struct timeval audio_tvn   = { 0 , 0 };
	struct timeval audio_tvs   = { 0 , 0 };
  long long frameDuration    = 0L ;

#ifdef MP4V2
	MP4TrackId text = -1;
  struct timeval CurrTextTv  = { 0 , 0 };
  struct timeval LastTextTv  = { 0 , 0 }; 
#endif

  int lastAudioSeqno  = 0;
  int lastVideoSeqno  = 0;
  int lastTextSeqno   = 0;
  int lostAudioPacket = 0;
  int lostVideoPacket = 0;
  int lostTextPacket  = 0;

	videoTrack.mp4 = 0;
	videoTrack.track = 0;
	videoTrack.hint = 0;
	videoTrack.length = 0;
	videoTrack.first = 0;  
	videoTrack.intra = 0;  

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

		/* Check video waiting */
		if (strchr(params,'s'))
		{
			/* Enable video loopback & waiting*/
			synchroVideo = 1;
		}

		if ( strchr(params, 't') )
		{
		    saveText = 1;
		}
	}


	ast_log(LOG_DEBUG, ">mp4save [%s,%s]\n",
          (char*)data,(params!=NULL)?params:"no params");

  if ( !haveVideo )
  {
    synchroVideo = 0 ;
    loopVideo    = 0 ;
    waitVideo    = 0 ;
  }
  else
  {
    // force video sync, else record dont stop 
    waitVideo = 1;
    ast_log(LOG_DEBUG, ">mp4save force wait video");
  }

  ast_log(LOG_DEBUG, ">mp4save loopVideo[%d] waitVideo[%d] "
          " synchroVideo[%d] saveText[%d] audio[%s] video[%s] txt[%s]\n",
          loopVideo,waitVideo,synchroVideo,saveText,
          (haveAudio)?"OK":"NONE",
          (haveVideo)?"OK":"NONE",
          (haveText) ?"OK":"NONE"); 

	/* Create mp4 file */
	mp4 = MP4CreateEx((char *) data, 9, 0, 1, 1, 0, 0, 0, 0);

	/* If failed */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
	    ast_log(LOG_ERROR, "Fail to create MP4 file %s.\n", (char*) data);
	    return -1;
	}



 /* Disable verbosity */
 MP4SetVerbosity(mp4, 0);
    
#ifndef i6net
 {
   char metadata[100];
   time_t now;
   struct tm *tmvalue; 

   time(&now);
   tmvalue = localtime(&now);
      
   MP4SetMetadataTool(mp4, "app_mp4");	  
   ast_copy_string(metadata, chan->cdr->dst, sizeof(chan->cdr->dst));
   MP4SetMetadataWriter(mp4,metadata);
   ast_copy_string(metadata, chan->cdr->src, sizeof(chan->cdr->src));
   MP4SetMetadataArtist(mp4,metadata);
   sprintf(metadata, "%04d/%02d/%02d %02d:%02d:%02d",
           tmvalue->tm_year+1900, tmvalue->tm_mon+1, tmvalue->tm_mday,
           tmvalue->tm_hour, tmvalue->tm_min, tmvalue->tm_sec);
   MP4SetMetadataAlbum(mp4,metadata);
   //MP4SetMetadataName(mp4,"Name");
   //MP4SetMetadataYear(mp4,"Year");
 }
#endif

 /* Lock module */
 u = ast_module_user_add(chan);

#ifdef VIDEOCAPS
   int oldnative = chan->nativeformats;
   if ( chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK )
   {
        chan->nativeformats =  chan->channelcaps.cap;
        ast_log(LOG_WARNING, "mp4_save: already received audio format %08x.\n",
                chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK)  ;
   }
   else
   {
        ast_log(LOG_WARNING, "mp4_save: using original native formats.\n");
   }

#endif


 int length = strlen(data);
 if (!strcmp(data+length-4, ".3gp"))
 {
	 if (ast_set_read_format(chan, AST_FORMAT_AMRNB))
     ast_log(LOG_WARNING, "mp4_save: Unable to set read format to AMRNB!\n");
 }
 else
 {
	 if (ast_set_read_format(chan, AST_FORMAT_ULAW|AST_FORMAT_ALAW|AST_FORMAT_AMRNB))
     ast_log(LOG_WARNING, "mp4_save: Unable to set read format to ULAW|ALAW|AMRNB!\n");
 }     

#ifdef VIDEOCAPS
    chan->nativeformats = oldnative;
#endif


 videoTrack.sampleId = 0;
 videoTrack.frame    = NULL;		
 timestampVideo      = 0;
 timestampAudio      = 0;
 correctionVideo     = 0;
 lastdurationVideo   = 0;
 lastdurationAudio   = 0;
 lastdurationText    = 0;
 VideoDuration       = 0;
 AudioDuration       = 0;
 TextDuration        = 0;

 if (maxduration <= 0)
   maxduration = -1;

 /* Send video update */
 ast_indicate(chan, AST_CONTROL_VIDUPDATE);

 /* Wait for data avaiable on channel */
 //while (ast_waitfor(chan, maxduration) > -1)
 while ((waitres = ast_waitfor(chan, maxduration)) > -1)
 { 
	 if ( !(waitVideo) && (maxduration > 0)) 
   {
     if (waitres == 0) 
     {
       gottimeout = 1;
       break;
     }
     maxduration = waitres;
   }
			
   // ast_log(LOG_DEBUG, "maxduration=%d waires=%d!\n", maxduration, waitres); 
   audio_tvn = video_tvn = text_tvn = ast_tvnow();
   if ( !(video_tvs.tv_sec) && !(video_tvs.tv_usec ))
   {
     video_tvs = video_tvn ;	 
   }
       
   if ( !(audio_tvs.tv_sec) && !(audio_tvs.tv_usec ))
   {
     audio_tvs = audio_tvn ;	 
   }

   if ( !(text_tvs.tv_sec) && !(text_tvs.tv_usec ))
   {
     text_tvs = text_tvn ;	 
   }

   /* Calculate elapsed */
   //t = ast_tvdiff_ms(tvn,tv);
   //ast_log(LOG_DEBUG, "delta = %d!\n", t);    


   /* Read frame from channel */
   f = ast_read(chan);

   /* if it's null */
		if (f == NULL)
    { 
      ast_log(LOG_DEBUG, "no frame \n");
			break;
    }


    switch ( f->frametype )
    {
      case AST_FRAME_VOICE:
        if ( lastAudioSeqno &&  ++lastAudioSeqno != f->seqno  ){
          lostAudioPacket += ABS(f->seqno-lastAudioSeqno);
          ast_log(LOG_WARNING, "%d packet lost (wait:%d recv:%d) on audio total lost[%d]\n",
                  ABS(f->seqno-lastAudioSeqno),lastAudioSeqno , f->seqno ,lostAudioPacket);
        }
        lastAudioSeqno = f->seqno ;
      break;
      case AST_FRAME_VIDEO:
        if ( lastVideoSeqno &&  ++lastVideoSeqno != f->seqno  ){
          lostVideoPacket += ABS(f->seqno-lastVideoSeqno);
          ast_log(LOG_WARNING, "%d packet lost (wait:%d recv:%d) on video stream, total lost[%d]\n",
                  ABS(f->seqno-lastVideoSeqno),lastVideoSeqno , f->seqno ,lostVideoPacket);
        }
        lastVideoSeqno = f->seqno ;
      break;
      case AST_FRAME_TEXT:
        if ( lastTextSeqno &&  ++lastTextSeqno != f->seqno   ){
          lostTextPacket += ABS(f->seqno-lastTextSeqno);
          ast_log(LOG_WARNING, "%d packet lost (wait:%d recv:%d) on text stream, total lost[%d]\n",
                  ABS(f->seqno-lastTextSeqno),lastVideoSeqno , f->seqno ,lostTextPacket);
        }
        lastTextSeqno = f->seqno ;
      break;
      default :
        break;
    }


		/* Check if we have to wait for video */
		// if ((f->frametype == AST_FRAME_VOICE) && (videoTrack.sampleId != 0)) 
		if (f->frametype == AST_FRAME_VOICE)
		{
#ifdef FIX_TIME 
			long timeAudio;
#endif
			int notwrite = 0;

      AudioDuration = ast_tvdiff_ms(audio_tvn,audio_tvs);			
      audio_tvs = audio_tvn ;

      // Build real time for ref .
      gettimeofday( &CurrAudioTv , NULL ) ;
      if ( !( LastAudioTv.tv_sec || LastAudioTv.tv_usec ))
      {
        // First packet 
        LastAudioTv = CurrAudioTv ;
        oldAudioTs = f->ts ;
      }
      DIFF_MS( LastAudioTv , CurrAudioTv , TimeDiff );
      LastAudioTv = CurrAudioTv ;

      if (option_debug > 4) 
      {
        ast_log(LOG_DEBUG, "AUDIO [%s] f->samples[%d] f->ts[%ld] f->len[%ld] Stack duration[%ld] Ref time[%lld]\n", 
                (f->subclass & AST_FORMAT_G729A )?"G729A"
                : (f->subclass &  AST_FORMAT_ULAW)?"ULAW"
                : (f->subclass & AST_FORMAT_ALAW )?"ALAW"
                : (f->subclass & AST_FORMAT_AMRNB)?"AMRNB":"??",
                f->samples,f->ts, f->len ,AudioDuration,TimeDiff  );    
      }
   
      if (waitVideo && videoTrack.sampleId == 0)
      {
        /* ignoring audio packet if wait video and video not started */
        if (option_debug > 4) 
        {
          ast_log(LOG_DEBUG, "AUDIO delete packet : waitVideo && videoTrack.sampleId == 0\n");
        }
        oldAudioTs = f->ts ;
        continue;
      }

      if (!waitVideo)
      {
        if (f->datalen!=7)
          timestampAudio += f->samples;
        else
          notwrite = 1;
      }

#ifdef FIX_TIME 
      if (synchroAudio)
      {
        f->samples = (AudioDuration - lastdurationAudio) * 8;
        lastdurationAudio = AudioDuration;
      }    

      timeAudio = (timestampAudio/ 8);
  
      if (option_debug > 5)
      {
        ast_log(LOG_DEBUG, "Audio duration = %ld, %ld, (%d)!\n", AudioDuration, timeAudio, timestampAudio); 
        if (f->subclass & AST_FORMAT_AMRNB)
          ast_log(LOG_WARNING, "AMR datas = %d, %d!\n", f->datalen, f->samples);
      }

      if ( haveVideo )
      {
        if (audio != -1)
        {
          while (  (timeAudio +100) < AudioDuration )     
          {
            if (option_debug > 5 ) 
            {
              ast_log(LOG_WARNING,">mp4_save: Correct+ audio %ld < video %ld!\n",
                      timeAudio,AudioDuration);
            }
            
            if (f->subclass & AST_FORMAT_ULAW)
              mp4_rtp_write_audio_silence(&audioTrack, 0, f2);
            if (f->subclass & AST_FORMAT_ALAW)   
              mp4_rtp_write_audio_silence(&audioTrack, 8, f2);
            if (f->subclass & AST_FORMAT_AMRNB)   
              mp4_rtp_write_audio_silence(&audioTrack, 32, f2);
            
            timestampAudio += 160;
            timeAudio += 20;       
          }

          if (timeAudio > (AudioDuration + 100))
          {
            if (option_debug > 5) 
            {
              ast_log(LOG_WARNING, ">mp4_save: Correct- !\n");
            }
            timestampAudio -= f->samples;
            notwrite = 1;
          }
        } 

        if (autocorrectionAudio)
        {
          // Retard ou perte.
          if ( AudioDuration > timeAudio )
          {
            if ((AudioDuration - timeAudio) > 160)
            {
              if (option_debug > 5) ast_log(LOG_WARNING, "Correction -!\n"); 
              timestampAudio += f->samples;
              f->samples += f->samples;       
            }
          }
          // Avance.
          if ( AudioDuration < timeAudio)
          {
            if ((timeAudio - AudioDuration ) > 160)
            {
              timestampAudio -= (f->samples - (f->samples / 2));
              f->samples = (f->samples / 2);       
              if (option_debug > 5) ast_log(LOG_WARNING, "Audio Correction f->samples[%d]\n",f->samples); 
            }
          }
        }
      }
#endif
     

			/* Check if we have the audio track for track creation  */
			if (audio == -1)
			{
				/* Check codec */
				if (f->subclass & AST_FORMAT_ULAW)
				{
					/* Create audio track */
					audio = MP4AddAudioTrack(mp4, 8000, 0, MP4_ULAW_AUDIO_TYPE);
					MP4SetTrackIntegerProperty(mp4, audio, 
                                     "mdia.minf.stbl.stsd.mp4a.channels", 1);
					MP4SetTrackIntegerProperty(mp4, audio, 
                                     "mdia.minf.stbl.stsd.mp4a.sampleSize", 8);
					/* Create audio hint track */
					hintAudio = MP4AddHintTrack(mp4, audio);
					/* Set payload type for hint track */
					type = 0;
					audio_payload = 0;
					MP4SetHintTrackRtpPayload(mp4, hintAudio, "PCMU", &type, 0, NULL, 1, 0);
          ast_log(LOG_DEBUG, ">mp4_save: add audio track mulaw\n");
				} 
        else if (f->subclass & AST_FORMAT_ALAW) 
        {
					/* Create audio track */
					audio = MP4AddAudioTrack(mp4, 8000, 0, MP4_ALAW_AUDIO_TYPE);
					MP4SetTrackIntegerProperty(mp4, audio, 
                                     "mdia.minf.stbl.stsd.mp4a.channels", 1);
					MP4SetTrackIntegerProperty(mp4, audio, 
                                     "mdia.minf.stbl.stsd.mp4a.sampleSize", 8);
					/* Create audio hint track */
					hintAudio = MP4AddHintTrack(mp4, audio);
					/* Set payload type for hint track */
					type = 8;
					audio_payload = 0;
					MP4SetHintTrackRtpPayload(mp4, hintAudio, "PCMA", &type, 0, NULL, 1, 0);
          ast_log(LOG_DEBUG, ">mp4_save: add audio track alaw\n");
				}
        else if (f->subclass & AST_FORMAT_AMRNB) 
        {
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
          ast_log(LOG_DEBUG, ">mp4_save: add audio track amr\n");
				} 
        else 
        {
          ast_log(LOG_DEBUG, ">mp4_save: Unknown audio codec \n");
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
			if (audio != -1)
				/* Save audio rtp packet */
				if (!notwrite)
        {
#ifndef FIX_TIME 
          // on met du silence si on en a perdu
          while ( f->ts > (oldAudioTs + 20)  ) // 20 car ts audio pour les codec ici = 20 ms 
          {
            if (option_debug > 4) 
            {
              ast_log(LOG_WARNING, "Add audio silence frame %d\n",audioTrack.sampleId);
            }
            if (f->subclass & AST_FORMAT_ULAW)
              mp4_rtp_write_audio_silence(&audioTrack, 0, f2);
            if (f->subclass & AST_FORMAT_ALAW)   
              mp4_rtp_write_audio_silence(&audioTrack, 8, f2);
            if (f->subclass & AST_FORMAT_AMRNB)   
              mp4_rtp_write_audio_silence(&audioTrack, 32, f2);

            mp4_rtp_write_audio(&audioTrack, f, audio_payload);
            oldAudioTs += 20 ; 
          }
#endif
          if (option_debug >  4 ) 
          {
            ast_log(LOG_WARNING, "Write audio frame %d\n",audioTrack.sampleId);
          }
          mp4_rtp_write_audio(&audioTrack, f, audio_payload);
          oldAudioTs = f->ts ;
        }
    } 
    else if (f->frametype == AST_FRAME_VIDEO) 
    {
			/* No skip and no add */
			int skip = 0;
			unsigned char *prependBuffer = NULL;
			unsigned char *frame = AST_FRAME_GET_BUFFER(f);
			int prependLength = 0;
			int intra = 0;
			int first = 0;
			int notwrite = 0;
#ifdef FIX_TIME 
			long timeVideo;
#endif

      VideoDuration = ast_tvdiff_ms(video_tvn,video_tvs);	
      video_tvs = video_tvn ;

      // Build real time for ref .
      gettimeofday( &CurrVideoTv , NULL ) ;
      if (  !(LastVideoTv.tv_sec) && !LastVideoTv.tv_usec )
      {
        LastVideoTv = CurrVideoTv ;
      }
      DIFF_MS( LastVideoTv , CurrVideoTv , TimeDiff );
      LastVideoTv = CurrVideoTv ;

      if (option_debug > 4) 
      {
        ast_log(LOG_DEBUG, "Video [%s] f->samples[%d]  f->ts[%ld] f->len[%ld] Stack dur[%ld] Ref time[%lld] frame dur[%lld]\n", 
                (f->subclass & AST_FORMAT_H263)?"H263"
                : (f->subclass & AST_FORMAT_H263_PLUS)?"H263+"
                : (f->subclass & AST_FORMAT_H264)?"H264":"??",
                f->samples  , f->ts, f->len ,VideoDuration,TimeDiff,frameDuration );    
      }
      frameDuration += TimeDiff  ;

#ifdef FIX_TIME  
      if (synchroVideo)
      {
        f->samples = (VideoDuration - lastdurationVideo) * 90;
        lastdurationVideo = VideoDuration;
      } 

      if (option_debug > 4) 
      {
        ast_log(LOG_DEBUG, "Video duration = %ld, %ld, (%d)!\n", 
               VideoDuration , lastdurationVideo, f->samples);    
      }


      if (!waitVideo)
        timestampVideo += f->samples;
   
      timeVideo = (timestampVideo/ 90);
  
      if (option_debug > 4) 
      {
        ast_log(LOG_DEBUG, "timevideo[%ld]\n",timeVideo); 
      }
   
      if (autocorrectionVideo)
      {   
        // Retard ou perte.
        if ( VideoDuration > timeVideo)
        {
          if ((VideoDuration - timeVideo) > 9000)
          {
            ast_log(LOG_WARNING, "Correction - !\n"); 
            timestampVideo += f->samples;
            f->samples += f->samples;       
          }
        }
        // Avance.
        if (VideoDuration < timeVideo)
        {
          if ((timeVideo - VideoDuration ) > 9000)
          {
            ast_log(LOG_WARNING, "Correction + !\n"); 
            timestampVideo -= (f->samples - (f->samples / 2));
            f->samples = (f->samples / 2);       
          }
        }
      }
#endif
			/* Check codec */
			if (f->subclass & AST_FORMAT_H263)
			{
				/* Check if it's an intra frame */
				intra = (frame[1] & 0x10) != 0;
				/* Check PSC for first packet of frame */
				if ( f->datalen>7 && (frame[4] == 0) && (frame[5] == 0) && ((frame[6] & 0xFC) == 0x80) )
					/* It's first */
					first = 1;
				/* payload length */
				video_payload = 4;
			} 
      else if (f->subclass & AST_FORMAT_H263_PLUS) 
      {
				/* Check if it's an intra frame */
				unsigned char p = frame[0] & 0x04;
				unsigned char v = frame[0] & 0x02;
				unsigned char plen = ((frame[0] & 0x1 ) << 5 ) | (frame[1] >> 3);
				//unsigned char pebit = frame[0] & 0x7;
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
					prependBuffer = (unsigned char *)"\0\0";
					prependLength = 2;
				}
			} 
      else if (f->subclass & AST_FORMAT_H264) 
      {
				/* Get packet type */
				unsigned char nal = frame[0];
        unsigned char type = nal & 0x1f;
					
        /* All intra & first*/
				intra = 1;
				first = 1;
				/* Set payload and skip */
				video_payload = 0;
				skip = 0;
								
				//if (type==H264_NAL_TYPE_IDR_SLICE)
				//intra = 0;
				//else
				//intra = 1;

				/* Check nal type */
				if (type==H264_NAL_TYPE_SEQ_PARAM)
				{
          h264_decode_t h264_decode;
				 
          //waitVideo = 0;
          notwrite = 1;
				 
					//ast_frfree(f);
          //continue;
          ast_log(LOG_DEBUG, "mp4_save: SPS length : %d!\n", f->datalen);
          //f->datalen = 13;
				
          if (f->datalen < 50)
          {
            memcpy(h264_buffer+4, f->data, f->datalen);
            h264_buffer[0] = 0;
            h264_buffer[1] = 0;
            h264_buffer[2] = 0;
            h264_buffer[3] = 0;
          }
          else
          {
            ast_frfree(f);
            continue;
          }
				
          //dump_buffer_hex("h264_buffer", h264_buffer, f->datalen+4); 
				
          if (h264_read_seq_info(h264_buffer, f->datalen+4, &h264_decode) == -1)
          {
            ast_log(LOG_ERROR, "mp4_save: Could not decode Sequence header !\n");
          }
          else
          {
            ast_log(LOG_NOTICE, "mp4_save: H264 Size : %dx%d!\n",
                    h264_decode.pic_width, h264_decode.pic_height);				
            ast_log(LOG_NOTICE, "mp4_save: H264 Profile/Level : %d %d = %s!\n", 
                    h264_decode.profile, h264_decode.level, 
                    h264_get_profile_level_string(h264_decode.profile, 
                                                  h264_decode.level));
          }				
				
          if (video == -1)
          {
            /* Should parse video packet to get this values */
            unsigned char AVCProfileIndication 	= h264_decode.profile; //2;
            unsigned char AVCLevelIndication	= h264_decode.level; //1;
            unsigned char AVCProfileCompat		= 1;
            MP4Duration h264FrameDuration;
					
            h264FrameDuration		= 1.0/10;
            /* Create video track */

            video = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, h264_decode.pic_width, h264_decode.pic_height, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);


            /* Create video hint track */
            hintVideo = MP4AddHintTrack(mp4, video);
            /* Set payload type for hint track */
            type = 99;
            MP4SetHintTrackRtpPayload(mp4, hintVideo, "H264", &type, 0, NULL, 1, 0);
					
            /* Set struct info */
            videoTrack.mp4 = mp4;
            videoTrack.track = video;
            videoTrack.hint = hintVideo;
            videoTrack.length = 0;
            videoTrack.sampleId = 0;
            videoTrack.first = 1;
            videoTrack.frame = malloc(70000);
          }				
				
          //dump_buffer_hex("H264_NAL_TYPE_SEQ_PARAM", f->data, f->datalen); 
          MP4AddH264SequenceParameterSet(mp4,  video, f->data, f->datalen);
				 
          //ast_frfree(f);
          //continue;
				}

        if (video != -1 && type==H264_NAL_TYPE_PIC_PARAM)
        {
          notwrite = 1;

          //ast_frfree(f);
          //continue;
          ast_log(LOG_DEBUG, "mp4_save: PPS length : %d!\n", f->datalen);
          //f->datalen = 

          //dump_buffer_hex("H264_NAL_TYPE_PIC_PARAM", f->data, f->datalen); 				 
          MP4AddH264PictureParameterSet(mp4, video,  f->data, f->datalen);
				      
          if ((video !=-1) && (waitVideo==1))
          {
            waitVideo = 0;
            // video_tvs = ast_tvnow();
            ast_log(LOG_WARNING, "mp4save: H264_NAL_TYPE_PIC_PARAM Unlock WaitVideo!\n");
          }  				      
				 
          //ast_frfree(f);
          //continue;
        }

				if (type==H264_NAL_TYPE_SEI)
				{
          //ast_frfree(f);
          //continue;
          notwrite = 1;
				}

        if (type == 0x01C)
        {         
          // these are the same as above, we just redo them here for clarity...
          uint8_t fu_indicator = nal;
          uint8_t fu_header = frame[1];   // read the fu_header.
          uint8_t start_bit = fu_header >> 7;
//            uint8_t end_bit = (fu_header & 0x40) >> 6;
          uint8_t nal_type = (fu_header & 0x1f);
          uint8_t reconstructed_nal;            
          
          // reconstruct this packet's true nal; only the data follows..
          reconstructed_nal = fu_indicator & (0xe0);  // the original nal forbidden bit and NRI are stored in this packet's nal;
          reconstructed_nal |= nal_type;
            
          //ast_log(LOG_DEBUG, "mp4recorder: start_bit In : %d, reconstructed_nal : %d !\n", start_bit, reconstructed_nal);

          /* And add the data to the frame but not associated with the hint track */
          if (start_bit)
          {
            fulength = f->datalen -1;
            h264_buffer[0] = (fulength >> 24) & 0xFF;
            h264_buffer[1] = (fulength >> 16) & 0xFF;
            h264_buffer[2] = (fulength >> 8) & 0xFF;
            h264_buffer[3] = (fulength & 0xFF);
            h264_buffer[4] = reconstructed_nal;
            prependBuffer = h264_buffer;
            prependLength = -5;
            intra = 1;
             
            if ((video != -1) && (waitVideo))
            {
              waitVideo = 0;
              //  tvs = ast_tvnow();
              ast_log(LOG_WARNING, "mp4save: Start_bit : Unlock WaitVideo!\n");
            }            
          } 
          else         
          {
            fulength += (f->datalen -2);
            h264_buffer[0] = (fulength >> 24) & 0xFF;
            h264_buffer[1] = (fulength >> 16) & 0xFF;
            h264_buffer[2] = (fulength >> 8) & 0xFF;
            h264_buffer[3] = (fulength & 0xFF);
            prependBuffer = h264_buffer;
            prependLength = -4;
            intra = 1;
          } 
            
          /* Set payload and skip */
          video_payload = 0;
          skip = 2;            
        }
              				
				if (type < H264_NAL_TYPE_SEI)
				{
					/* And add the data to the frame but not associated with the hint track */
					int length = f->datalen;
					
					h264_buffer[0]=0;
          h264_buffer[1]=(length >> 16) & 0xFF;
          h264_buffer[2]=(length >> 8) & 0xFF;
          h264_buffer[3]=(length & 0xFF);
					
					prependBuffer = h264_buffer;
					prependLength = 4;
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

#ifdef FIX_TIME
			if (notwrite)
			{
			  correctionVideo += f->samples;
        ast_log(LOG_DEBUG, "mp4save: correctionVideo %d!\n", correctionVideo);
      }
      else
      {
        if (videoTrack.first == 0)
        {
          correctionVideo += f->samples;
          if (option_debug > 4) ast_log(LOG_DEBUG, "mp4save: correctionVideo %d!\n", correctionVideo);
        }
        else   
          if (correctionVideo)
          {
            f->samples += correctionVideo;      
            correctionVideo = 0;
      
            if (option_debug > 4) ast_log(LOG_DEBUG, "mp4save: correctionVideo %d -> %d!\n", correctionVideo, f->samples);
          }
      }
#endif

			/* Check if we have to wait for video */
			if (waitVideo)
			{
				/* If it's the first packet of an intra frame */
				if (first && intra)
				{
					/* no more waiting */
					waitVideo = 0;
					// tvs = ast_tvnow();
          ast_log(LOG_WARNING, "mp4save: first && intra : Unlock WaitVideo!\n");
				} else {
					/* free frame */
					ast_frfree(f);
					/* Keep on waiting */
          ast_log(LOG_WARNING, "mp4save: first && intra : Delete video frame\n");
					continue;
				}
			}

			/* Check if we have the video track */
			if (video == -1)
			{
				/* Check codec */
				if (f->subclass & AST_FORMAT_H263)
				{
					/* Create video track */
					video = MP4AddH263VideoTrack(mp4, 90000, 0, 176, 144, 0, 0, 0, 0);									
					/* Create video hint track */
					hintVideo = MP4AddHintTrack(mp4, video);
					/* Set payload type for hint track */
					type = 34;
					MP4SetHintTrackRtpPayload(mp4, hintVideo, "H263", &type, 0, NULL, 1, 0);

					/* Set struct info */
          videoTrack.mp4 = mp4;
          videoTrack.track = video;
          videoTrack.hint = hintVideo;
          videoTrack.length = 0;
          videoTrack.sampleId = 0;
          videoTrack.first = 1;
          videoTrack.frame = malloc(70000);
          videoTrack.timeScale = 90000;
				} 
        else if (f->subclass & AST_FORMAT_H263_PLUS) {
					/* Create video track */
					video = MP4AddH263VideoTrack(mp4, 90000, 0, 176, 144, 0, 0, 0, 0);
					/* Create video hint track */
					hintVideo = MP4AddHintTrack(mp4, video);
					/* Set payload type for hint track */
					type = 96;
					MP4SetHintTrackRtpPayload(mp4, hintVideo, "H263-1998", &type, 0, NULL, 1, 0);
					
					/* Set struct info */
          videoTrack.mp4 = mp4;
          videoTrack.track = video;
          videoTrack.hint = hintVideo;
          videoTrack.length = 0;
          videoTrack.sampleId = 0;
          videoTrack.first = 1;
          videoTrack.frame = malloc(70000);
          videoTrack.timeScale = 90000;
				} 			
			}

			/* If we have created the track */
			if (video != -1)
        if (!notwrite)
        {
          if ( f->subclass & 0x1 )
          {
            if (option_debug > 4) ast_log(LOG_DEBUG, "mp4save:mark bit\n");
          }

          if ( videoTrack.first )
          {
            // 90 car sample = nombre d'echantillon , la video est exprimé a 90 000 000 en nano sec , soit 90 en milli 
            // 1000 car temps exprime en micro et asterisk en milli 
            f->samples = frameDuration * 90 / 1000 ; 
            if (option_debug > 4) ast_log(LOG_DEBUG, "mp4save:first f->samples[%d]\n",f->samples);
            frameDuration = 0 ;
          }
          else
          {
            f->samples = 0 ;
          }

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

		} 
    else if (f->frametype == AST_FRAME_DTMF) 
    {
			/* If it's the dtmf param */
			if (params && strchr(params,f->subclass))
			{
				/* free frame */
				ast_frfree(f);
				/* exit */
				break;
			}
		}
		else if (f->frametype == AST_FRAME_TEXT) 
		{
        if (f->subclass == AST_FORMAT_RED)
        {
			    /* T140 redondant */
			    struct ast_frame *pf, *red[MAX_REDUNDANCY_LEVEL];

			    pf = decode_redundant_payload( f, red );
			    if (pf != NULL && pf->datalen > 0 )  
			    {
            int idx = 0 ;
            if ( (IdxTxtBuff + pf->datalen) < AST_MAX_TXT_SIZE )
            {
              suppressT140BOM((char*)pf->data ,(size_t*)&pf->datalen );
              if (option_debug > 1)
              {
                char txt[200];
                strncpy(txt, pf->data, 200);
                txt[pf->datalen] = '\0';
                ast_log(LOG_DEBUG, "Saving %d char of redundant text : [%s].\n", 
                        pf->datalen, txt);
              }

              while ( idx <  pf->datalen )
              {
                if ( (((char*)pf->data)[idx] == 0x08) && ( IdxTxtBuff > 1 )  )
                {
                  // 08  == $(B!G(B\b$(B!G(B (effacement arrière)  
                  IdxTxtBuff -- ;
                }
                else if (IdxTxtBuff < AST_MAX_TXT_SIZE )
                {
                  // write txt on tmp buff 
                  txtBuff[IdxTxtBuff]= ((char*)pf->data)[idx];
                  IdxTxtBuff ++ ;
                  txtBuff[IdxTxtBuff]=0;
                  ast_log(LOG_DEBUG, ">%s\n",txtBuff);
                }
                else
                {
                  ast_log(LOG_WARNING , "Size of buff are too small (%d) "
                          "- or buff are emptye and char is 0x08,"
                          " can't save all the text\n",AST_MAX_TXT_SIZE);
                }
                idx++;
              }
            }
            else
            {
              ast_log(LOG_WARNING , "Size of buff are too small (%d),"
                      " can't save all the text\n",AST_MAX_TXT_SIZE);
              // buf too small , print a marker for futur information 
              size_t szMark = sizeof(mark_cut_txt);
              size_t szRes  = AST_MAX_TXT_SIZE - IdxTxtBuff  ;
              // write begin of txt on tmp buff 
              memcpy( &txtBuff[IdxTxtBuff],pf->data,szRes);
              memcpy( &txtBuff[AST_MAX_TXT_SIZE - szMark], mark_cut_txt,szMark);
              txtBuff[AST_MAX_TXT_SIZE]=0;
              IdxTxtBuff = AST_MAX_TXT_SIZE ;
            }
			    }

			    int i;
			    for (i=0; i < MAX_REDUNDANCY_LEVEL; i++)
			    {
            if (red[i]) ast_frfree(red[i]);
			    }
        }
        else
        {
			    if (f->datalen > 0) 
			    {
            int idx = 0 ;
            suppressT140BOM( (char*)f->data , (size_t*)&f->datalen );
            if (option_debug > 1)
              ast_log(LOG_DEBUG, "Saving %d [0x%X] char of text.\n", f->datalen,((char*)f->data)[0]);

            while ( idx <  f->datalen )
            {
              if ( (((char*)f->data)[idx] == 0x08) && ( IdxTxtBuff > 1 )  )
              {
                // 08  == $(B!G(B\b$(B!G(B (effacement arrière)  
                IdxTxtBuff -- ;
              }
              else if (IdxTxtBuff < AST_MAX_TXT_SIZE )
              {
                if ( isprint( ((char*)f->data)[idx]) )
                {
                  // write txt on tmp buff 
                  txtBuff[IdxTxtBuff]= ((char*)f->data)[idx];
                  IdxTxtBuff ++ ;
                }
                else  ast_log(LOG_WARNING , "Ignore char idx[%d] car. code[0x%x]\n",(int)IdxTxtBuff,((char*)f->data)[idx]);
              }
              else
              {
                ast_log(LOG_WARNING , "Size of buff are too small (%d) "
                        "- or buff are emptye and char is 0x08,"
                        " can't save all the text\n",AST_MAX_TXT_SIZE);
              }
              idx++;
            }
			    }
        }

		}

		/* If we have frame */
		if (f)
			/* free frame */
			ast_frfree(f);
	}

	/* Save last video frame if needed */
	if (videoTrack.sampleId > 0)
	{	
		//ast_log(LOG_DEBUG, "mp4_save: last pending sample.\n");
		mp4_rtp_write_video_frame(&videoTrack, 9000);
	}


  if ( IdxTxtBuff > 1 )
  {  
    if (option_debug > 1)
    {
      ast_log(LOG_DEBUG, "Save text on mp4 : %s.\n", 
             txtBuff );
    }
    if ( !MP4SetMetadataComment(mp4,txtBuff) )
    {
      ast_log(LOG_ERROR, "Save text on mp4 failed");
    }
  }

  {    
    char rtpStat[_STR_CODEC_SIZE]= { 0 } ;
    snprintf( rtpStat , _STR_CODEC_SIZE , "Quality_stat_audLost=%d,__vidLost=%d,__txtLost=%d.",
              lostAudioPacket, lostVideoPacket ,lostTextPacket );
    if (lostAudioPacket||lostVideoPacket||lostTextPacket)
    {
      ast_log(LOG_WARNING, "%s\n",rtpStat );
    }
    if ( !MP4SetMetadataAlbum(mp4,rtpStat) )
    {
      ast_log(LOG_ERROR, "Save textrtp stat on mp4 failed");
    }
  }

	/* Close file */
	MP4Close(mp4);

	if (videoTrack.frame)
	    free(videoTrack.frame);
    
	/* Unlock module*/
	ast_module_user_remove(u);

	//Success
	return 0;
}


static void suppressT140BOM(char* buff,size_t* sz )
{
#define KEEP_ALIVE_BOM_UTF8         {0xEF,0xBB,0xBF}
#define KEEP_ALIVE_BOM_UTF8_SZ      3

#define KEEP_ALIVE_BOM_UTF16        { 0xFE , 0xFF }
#define KEEP_ALIVE_BOM_UTF16_SZ     2

	char bomUtf16[KEEP_ALIVE_BOM_UTF16_SZ]	= KEEP_ALIVE_BOM_UTF16;
	char bomUtf8[KEEP_ALIVE_BOM_UTF8_SZ]	= KEEP_ALIVE_BOM_UTF8;
  char*  seq = buff;
  size_t len = *sz ;

  if (option_debug > 1)
    ast_log(LOG_DEBUG, "suppressT140BOM buff[%p] sz[%ld]\n",buff,len);


	while ( seq != NULL && buff != NULL && len > 0 )
	{
		seq = strstr( buff, bomUtf16 );
		if (seq != NULL)
		{
			 if (option_debug > 1)
         ast_log(LOG_DEBUG, " UTF 16 BOM detected.\n");
       // On decale le reste de la chaine pour supprimer le BOM
       int lgRestante = len - ( seq - buff );
       memmove( seq, seq + 1, lgRestante );
		}		
		len = strlen(buff);
		
		if (len > 3)
		{
			seq = strstr( buff, bomUtf8 );
		}
		else
		{
			seq = NULL;
		}
		
		if (seq != NULL)
		{
		 if (option_debug > 1)
         ast_log(LOG_DEBUG, " UTF 8 BOM detected.\n");
			// On decale le reste de la chaine pour supprimer le BOM
			int lgRestante = len - ( seq - buff ) - 2;
			memmove( seq, seq + 3, lgRestante ) ;
		}
		len = strlen(buff);
    *sz = len ;
	}
  buff[len]=0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_play);
	res &= ast_unregister_application(app_save);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(app_save, mp4_save, syn_save, des_save);
	res &= ast_register_application(app_play, mp4_play, syn_play, des_play);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "MP4 applications");

