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

#include <asterisk.h>
#include <mp4v2/mp4v2.h>
#include <astmedkit/framebuffer.h>
#include <astmedkit/framebuffer.h>

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

enum 
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

AST_APP_OPTIONS(mp4play_exec_options, 
{
        AST_APP_OPTION_ARG('S', OPT_DFTMINTOVAR, OPT_ARG_DFTMINTOVAR),
        AST_APP_OPTION_ARG('n', OPT_NOOFDTMF, OPT_ARG_NOOFDTMF),
	AST_APP_OPTION_ARG('s', OPT_STOPDTMF, OPT_ARG_STOPDTMF),
});



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
	char *params = NULL;
	int maxduration = 1000*1200;		/* max duration of recording in milliseconds - 20 mins */
	int remainingduration = maxduration;
	int waitres;
	char stopDtmfs[20] = "#";
	struct mp4rec * recorder;
	char metadata[100];
	
	/*  whether we send back the video packets to the caller */
	int videoLoopback = 0;
	
	/*  whether we wait for video I-frame to start recording */
	int waitVideo = 0;
	
	/*  Recording is on man! */
	int onrecord = 1;
	
	long VideoDuration;
	long AudioDuration;
	long TextDuration;
  
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
		
		int j = strlen(stopDtmfs);
		for (int i=0; i < strlen(params); i++)
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

	ast_log(LOG_DEBUG, ">mp4save [%s,%s]\n", (char*)data,(params!=NULL)?params:"no params");

	ast_log(LOG_DEBUG, ">mp4save media - audio[%s] video[%s] text[%s] max duration %d sec (0=infinite)\n",
			(haveAudio)? "OK" : "NONE",
			(haveVideo)? "OK" : "NONE",
			(haveText) ? "OK" : "NONE",
			maxduration/1000 ); 

	/* Lock module */
	u = ast_module_user_add(chan);

	/* Create mp4 file */
	mp4 = MP4CreateEx((char *) data, 9, 0, 1, 1, 0, 0, 0, 0);

	/* If failed */
	if (mp4 == MP4_INVALID_FILE_HANDLE)
	{
	    ast_log(LOG_ERROR, "Fail to create MP4 file %s.\n", (char*) data);
	    goto mp4_save_cleanup;
	}

	recorder = Mp4RecorderCreate(chan, mp4, waitVideo, "h264@vga");

	if ( recorder == NULL )
	{
	    ast_log(LOG_ERROR, "Fail to create MP4 recorder. Exiting\n");
	    goto mp4_save_cleanup;
	}

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


	/* Disable verbosity */
	MP4SetVerbosity(mp4, 0);


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
			    (char) f->subclass )
			onrecord = 0;
			ast_frfree(f);
		    }
		    break;
		
		default:
	            ast_frfree(f);
	            break;
	    }
	    
	    /* -- now poll all the queues and record -- */
	    for (int i=0; i<3; i++)
	    {
		f = AstFbAddFrame( queueTab[i] );
		
		// TODO if too many errors, exit
		recorder->ProcessFrame(f);
		
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
	MP4Close(mp4);

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

