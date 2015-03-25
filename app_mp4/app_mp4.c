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
 *  6 mars 2014
 *      Reimplementation avec medkit
 */

/*! \file
 *
 * \brief MP4 application -- save and play mp4 files
 *
 * \ingroup applications
 */
#include <sys/time.h>


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
#include <asterisk/speech.h>

#include <mp4v2/mp4v2.h>
#include <astmedkit/mp4format.h>
#include <astmedkit/framebuffer.h>
#include <astmedkit/frameutils.h>

#include "h263packetizer.h"

#undef i6net
#undef i6net_lock

#ifdef i6net_lock
#include <app_vxml.h>
#endif


#ifndef AST_FORMAT_AMRNB
#define AST_FORMAT_AMRNB 	(1 << 13)
#endif

#ifndef _STR_CODEC_SIZE
#define _STR_CODEC_SIZE         512
#endif

#define PKT_OFFSET	(sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET)
#define AST_MAX_TXT_SIZE 0x8000 
#define NO_CODEC         -1
#define MS_2_SEC         1000000   // Micro secondes -> Sec 
#define MAX_DTMF_BUFFER_SIZE 25

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

enum _mp4play_exec_option_flags
{
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


#ifdef VIDEOCAPS
/*! \brief codec video dans le fichier 
 *  */
typedef enum 
{
  NATIVE_VIDEO_CODEC_H264 = 0 ,
  NATIVE_VIDEO_CODEC_H263P ,
  NATIVE_VIDEO_CODEC_H263 ,
  NATIVE_VIDEO_CODEC_LAST // Always last 
} NativeCodec;
#endif


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

static int mp4_play_process_frame(struct ast_frame *f, char * dtmfBuffer, const char * stopChars)
{
	if ( f == NULL) return -2;
	
	/* If it's a dtmf */
	if (f->frametype == AST_FRAME_DTMF)
	{
		/* Stop flag */
		bool stop = false;

		/* Get DTMF char */
		char dtmf[2];
		dtmf[0] = f->subclass;
		dtmf[1] = 0;

		/* Check if it's in the stop char digits */
		if (stopChars && strchr(stopChars,dtmf[0])) 
		{
			/* Clean DMTF input */
			strcpy(dtmfBuffer,dtmf);
			/* Stop */
			stop = true;
			/* Continue after exit */
			res = 0;
			/* Log */
			ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play interrupted bv DTMF %s\n", dtmf);

			
					/* Check if we have to append the DTMF and wait for more than one digit */
		} 
		else if (numberofDigits>0) {
			/* Append to the DTMF buffer */
			strcat(dtmfBuffer,dtmf);
			/* Check length */
			if (strlen(dtmfBuffer)>=numberofDigits) {
				/* Continue after exit */
				res = 0;
				/* Stop */
				stop = true;	
				ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play stops: all expected DTMF have been input.\n", dtmf);
			}
		} 
		/* Check for dtmf extension in context */
		else if (ast_exists_extension(chan, chan->context, dtmf, 1, NULL)) {
			/* Set extension to jump */
			//res = f->subclass;
			/* Clean DMTF input */
			
			dtmfBuffer[0] = '\0';
			stop = true;
			ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play stops: jump to extention %s on DTMF.\n", dtmf);
			return -3;
		}
					
		/* If we have to stop */
		if (stop) 
		{
			/* Check DTMF variable`option*/
			if (varName)
				/* Build variable */
				pbx_builtin_setvar_helper(chan, varName, dtmfBuffer);
			ast_frfree(f);
			return -1;
		}
	}
	/* Free frame */
	ast_frfree(f);
	return 0;
}



static int mp4_play(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	MP4FileHandle mp4;
	struct mp4play * player;
	const char *type = NULL;
	int totalAudio = 0;
	int totalVideo = 0;

	char *parse;
	int numberofDigits = -1;
	char *varName = NULL;
	char *stopChars = NULL;
	char dtmfBuffer[MAX_DTMF_BUFFER_SIZE];
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	
	char cformat1[200] = {0};
	char cformat2[_STR_CODEC_SIZE] = {0};
	struct timeval tv, tvs;
	int ms = 0;
	
	//struct ast_frame *f = (struct ast_frame *) buffer;
	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(filename); AST_APP_ARG(options););

	/* Check for data */
	if (!data || ast_strlen_zero(data)) {
                ast_log(LOG_WARNING, "mp4play requires an argument (filename)\n");
                return -1;
        }

	ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play [%s].\n", args.filename);

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
		ast_log(LOG_WARNING, "mp4play cannot parse options\n");
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

	
	if (args.filename[0] != '/')
	{
	    struct stat s;
	    /* Use standard asterisk sound directory */
	    sprintf(cformat1, "%s/sounds/%s/%s",
                     ast_config_AST_DATA_DIR, chan->language, args.filename);
	
	    if ( stat(cformat1, &s) < 0 )
	    {
		// Could not open file. Try without language
		sprintf(cformat1, "%s/sounds/%s",
                     ast_config_AST_DATA_DIR, args.filename);
	    }
	}
	else
	{
		/* Use absolute path */
		strcpy( cformat1, args.filename );
	}
	
	mp4 = MP4Read(cformat1);
	
	/* If not valid */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
		ast_log(LOG_WARNING, "mp4play: failed to open file %s.\n", cformat1);
		/* exit */
		res = -1;
		goto clean;
	}
	
	player = Mp4PlayerCreate(chan, mp4, true, 0);
	if ( player == NULL)
	{
		ast_log(LOG_WARNING, "mp4play: failed to create MP4 player for file %s.\n", args.filename);
		res = -1;
		goto end;
	}
	
	ast_log(LOG_DEBUG, "Native formats:%s , Chann capability ( videocaps.cap ):%s\n", 
		ast_getformatname_multiple(cformat1,_STR_CODEC_SIZE, chan->nativeformats),
		ast_getformatname_multiple(cformat2,_STR_CODEC_SIZE, chan->channelcaps.cap));


	tv = ast_tvnow();
	
	while ( ms >= 0 )
	{
	    ms = Mp4PlayerPlayNextFrame(player,chan);
	    
	    if (ms < 0)
	    {
	        if (ms == -1)
		{
		    ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play [%s] stopped (end of file))", args.filename); 
		    res = 0;
		}
		else
		{
		    ast_log(LOG_ERROR, "mp4play: failed to play file %s. Error code=%d.\n", 
			    args.filename, ms);
		    res = -1;
		}
		break;
	    }
	    
	    // Wait x ms 
	    while (ms > 0)
	    {
		ms = ast_waitfor(chan, ms);
		tvs = ast_tvnow();
		//
		// compute global timestamp to check if we are late
		// proctime = ast_tvdiff_ms(tvs, tv);
		
		if (ms > 0)
		{
		    int proctime;
		    struct ast_frame * f = ast_read(chan);
		    res = mp4_play_process_frame(f, dtmfBuffer);
		    if (res < 0)
		    {
			if (res == -3) 
			{
			    // Jump to extension (is it still valid ?)
			    res = f->subclass;
			    ast_frfree(f);
			}
			else
			    res = -1;
			break;
		    }
		    
		    proctime = ast_tvdiff_ms(ast_tvnow(), tvs);
		    ms -= proctime;
		}
		else if (ms < 0)
		{
		    ast_verbose(VERBOSE_PREFIX_3 " -- MP4Play [%s] stopped (hangup))", args.filename);
		    res = 0;
		}
	    }
	}
	
end:
	if (player != NULL) Mp4PlayerDestroy(player);
	/* Close file */
	MP4Close(mp4, 0);

clean:
	/* Unlock module*/
	ast_module_user_remove(u);

	/* Free datra*/
	free(parse);

	/* Exit */
	return res;
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



static int mp4_save(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u = NULL;
	struct ast_frame *f = NULL;
	char *params = NULL;
	int maxduration = 1000*1200;		/* max duration of recording in milliseconds - 20 mins */
	int remainingduration = maxduration;
	int waitres;
	char stopDtmfs[20] = "#";
	struct mp4rec * recorder;
	char metadata[100];
	MP4FileHandle mp4;
	
	/*  whether we send back the video packets to the caller */
	int videoLoopback = 0;
	
	/*  whether we wait for video I-frame to start recording */
	int waitVideo = 0;
	
	/*  Recording is on man! */
	int onrecord = 1;
	
  
	int haveAudio           =  chan->nativeformats & AST_FORMAT_AUDIO_MASK ;
	int haveVideo           =  chan->nativeformats & AST_FORMAT_VIDEO_MASK ;  
	int haveText            =  chan->nativeformats & AST_FORMAT_TEXT_MASK ;  
	
	
	struct AstFb * audioInQueue;
	struct AstFb * videoInQueue;
	struct AstFb * textInQueue;

	struct AstFb * queueTab[3];
	
	/* Check for file */
	if (!data) return -1;

	
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
			videoLoopback = 1;
		}

		/* Check video waiting */
		if (strchr(params,'V'))
		{
			/* Enable video loopback & waiting*/
			videoLoopback = 1;
			waitVideo = 1;
		}
		
		int i, j = strlen(stopDtmfs);
		for (i=0; i < strlen(params); i++)
		{
		
			if ( (params[i] >= '0' && params[i] <= '9') 
			     || 
			     params[i] == '*' )
			{	
				stopDtmfs[j++] = params[i];
				stopDtmfs[j] = '\0';
			}
		}
	}

	ast_verbose(VERBOSE_PREFIX_3 
		    " -- MP4Save [%s], maxduration=%ds", 
		   (char*)data, maxduration/1000);

	/* Lock module */
	u = ast_module_user_add(chan);

	/* Create mp4 file */
	mp4 = MP4Create((char *) data,  0);

	/* If failed */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
	    ast_log(LOG_ERROR, "Fail to create MP4 file %s.\n", (char*) data);
	    goto mp4_save_cleanup;
	}

	time_t now;
	struct tm *tmvalue; 
	const MP4Tags * tags = MP4TagsAlloc();

	time(&now);
	tmvalue = localtime(&now);
	MP4TagsSetEncodingTool(tags, "MP4Save asterisk application");
	MP4TagsSetArtist(tags, chan->cid.cid_name );

        sprintf(metadata, "%04d/%02d/%02d %02d:%02d:%02d",
           tmvalue->tm_year+1900, tmvalue->tm_mon+1, tmvalue->tm_mday,
           tmvalue->tm_hour, tmvalue->tm_min, tmvalue->tm_sec);

	MP4TagsSetReleaseDate (tags, metadata);
	MP4TagsStore(tags, mp4);

	recorder = Mp4RecorderCreate(chan, mp4, waitVideo, "h264@vga", NULL);

	if ( recorder == NULL )
	{
	    ast_log(LOG_ERROR, "Fail to create MP4 recorder. Exiting\n");
	    goto mp4_save_cleanup;
	}

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
		if ( ast_set_read_format(chan, AST_FORMAT_AMRNB) )
			ast_log(LOG_WARNING, "mp4_save: Unable to set read format to AMRNB!\n");
	}
	else
	{
		if ( ast_set_read_format(chan, AST_FORMAT_ALAW) )
			ast_log(LOG_WARNING, "mp4_save: Unable to set read format to ALAW!\n");
	}     

#ifdef VIDEOCAPS
	chan->nativeformats = oldnative;
#endif

	/* no max duration */
	if (maxduration <= 0) remainingduration = -1;

	/* Send video update */
	ast_indicate(chan, AST_CONTROL_VIDUPDATE);

	audioInQueue = AstFbCreate(3, 0);
	videoInQueue = AstFbCreate(3, 0);
	textInQueue = AstFbCreate(0, 0);
	
	queueTab[0] = audioInQueue;
	queueTab[1] = videoInQueue;
	queueTab[2] = textInQueue;
	
	while ( onrecord )
	{
	    waitres = ast_waitfor(chan, remainingduration);
	    if ( waitres < 0 )
	    {
		/* hangup or error - trace ?*/
		onrecord = 0;
	    }
	    
	    if ( maxduration > 0 ) 
	    {
		if (waitres == 0) 
		{
		    ast_log(LOG_NOTICE, "Max recording duration %d seconds elapsed. Recording will stop.\n", 
			    maxduration/1000);
		    onrecord = 0;
		}
		else
		{
		    remainingduration = waitres;
		}
	    }

	    /* Read frame from channel */
	    f = ast_read(chan);

	    /* if it's null */
	    if (f == NULL)
	    { 
		ast_log(LOG_WARNING, "null frame: hangup ?\n");
		    onrecord = 0;;
	    }

	    /* --- post all media frames in a reorder buffer --- */
	    switch ( f->frametype )
	    {
		case AST_FRAME_VOICE:
	           AstFbAddFrame( audioInQueue, f );
		   ast_frfree(f);
	           break;
	       
		case AST_FRAME_VIDEO:
	            AstFbAddFrame( videoInQueue, f );
		    ast_frfree(f);
		    break;

		case AST_FRAME_TEXT:
	           AstFbAddFrame( textInQueue, f );
		   ast_frfree(f);
	           break;
	    
		case AST_FRAME_DTMF:
	            if (strchr( stopDtmfs, f->subclass) )
	            {
			ast_log(LOG_NOTICE, 
		            "mp4_save: recording stopping because DTMF %c was pressed.\n", 
			    (char) f->subclass );
			onrecord = 0;
			ast_frfree(f);
		    }
		    break;
		
		default:
	            ast_frfree(f);
	            break;
	    }
	    
	    /* -- now poll all the queues and record -- */
	    int i;
	    for (i=0; i<3; i++)
	    {
		f = AstFbGetFrame( queueTab[i] );
		
		// TODO if too many errors, exit
		Mp4RecorderFrame(recorder, f);
		
		if (  f->frametype == AST_FRAME_VIDEO )
		{
		    // TODO: if there are lost packets, ask FIR
		    if (videoLoopback) 
		    {
			/* -- ast_write() destroys the frame -- */
			ast_write(chan, f);
			f = NULL;
		    }
		}
		
		if ( f != NULL)  ast_frfree(f);
	    }
	}
	
mp4_save_cleanup:	    
	    
	
	/* destroy resources */
	if (recorder) Mp4RecorderDestroy(recorder);
	if (audioInQueue) AstFbDestroy(audioInQueue);
	if (videoInQueue) AstFbDestroy(videoInQueue);
	if (textInQueue) AstFbDestroy(textInQueue);
	
	/* Close file */
	MP4Close(mp4, 0);

	/* Unlock module*/
	ast_module_user_remove(u);

	//Success
	return 0;
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

