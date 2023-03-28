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
#include <sys/stat.h>

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/utils.h>
#include <asterisk/paths.h>
#include <asterisk/app.h>
#include <asterisk/version.h>
#include <asterisk/speech.h>

#include <mp4v2/mp4v2.h>
#include <astmedkit/mp4format.h>
#include <astmedkit/framebuffer.h>
#include <astmedkit/astlog.h>


#undef i6net
#undef i6net_lock


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

static const char mark_cut_txt[] = " Buff too small supress end of text";
static const char h263VideoName[] = "H.263";
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
" '0'..'9','#','*': sets dtmf input to stop recording\n"
" 'T': do not record text separate text file\n"
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
    OPT_DFTMINTOVAR = (1 << 0),
    OPT_NOOFDTMF = (1 << 1),
    OPT_STOPDTMF = (1 << 2),
} mp4play_exec_option_flags;

enum
{
    OPT_ARG_DFTMINTOVAR = 0,
    OPT_ARG_NOOFDTMF,
    OPT_ARG_STOPDTMF,
        /* note: this entry _MUST_ be the last one in the enum */
        OPT_ARG_ARRAY_SIZE,
} mp4play_exec_option_args;

AST_APP_OPTIONS( mp4play_exec_options, {
    AST_APP_OPTION_ARG( 'S', OPT_DFTMINTOVAR, OPT_ARG_DFTMINTOVAR ),
    AST_APP_OPTION_ARG( 'n', OPT_NOOFDTMF, OPT_ARG_NOOFDTMF ),
    AST_APP_OPTION_ARG( 's', OPT_STOPDTMF, OPT_ARG_STOPDTMF ),
    } );


#ifdef VIDEOCAPS
/*! \brief codec video dans le fichier
 *  */
typedef enum
{
    NATIVE_VIDEO_CODEC_H264 = 0,
    NATIVE_VIDEO_CODEC_H263P,
    NATIVE_VIDEO_CODEC_H263,
    NATIVE_VIDEO_CODEC_LAST // Always last 
} NativeCodec;
#endif



static int mp4_play_process_frame( struct ast_channel *chan, struct ast_frame *f, char *dtmfBuffer, const char *stopChars,
    int numberofDigits, const char *varName )
{
    int res;

    if( f == NULL ) return -2;

    /* If it's a dtmf */
    if( f->frametype == AST_FRAME_DTMF )
    {
        /* Stop flag */
        bool stop = false;

        /* Get DTMF char */
        char dtmf[2];
        dtmf[0] = f->subclass;
        dtmf[1] = 0;

        /* Check if it's in the stop char digits */
        if( stopChars && strchr( stopChars, dtmf[0] ) )
        {
            /* Clean DMTF input */
            strcpy( dtmfBuffer, dtmf );
            /* Stop */
            stop = true;
            /* Continue after exit */
            res = 0;
            /* Log */
            ast_verbose( VERBOSE_PREFIX_3 " MP4Play interrupted by DTMF %s\n", dtmf );


                    /* Check if we have to append the DTMF and wait for more than one digit */
        }
        else if( numberofDigits > 0 )
        {
/* Append to the DTMF buffer */
            strcat( dtmfBuffer, dtmf );
            /* Check length */
            if( strlen( dtmfBuffer ) >= numberofDigits )
            {
/* Continue after exit */
                res = 0;
                /* Stop */
                stop = true;
                ast_verbose( VERBOSE_PREFIX_3 " MP4Play stops: all expected %d DTMF have been entered.\n", numberofDigits );
            }
        }
        /* Check for dtmf extension in context */
        else if( ast_exists_extension( chan, chan->context, dtmf, 1, NULL ) )
        {
/* Set extension to jump */
//res = f->subclass;
/* Clean DMTF input */

            dtmfBuffer[0] = '\0';
            stop = true;
            ast_verbose( VERBOSE_PREFIX_3 " -- MP4Play stops: jump to extention %s on DTMF.\n", dtmf );
            return -3;
        }

        /* If we have to stop */
        if( stop )
        {
            /* Check DTMF variable`option*/
            if( varName )
                /* Build variable */
                pbx_builtin_setvar_helper( chan, varName, dtmfBuffer );
            ast_frfree( f );
            return -1;
        }
    }
    /* Free frame */
    ast_frfree( f );
    return 0;
}



static int mp4_play( struct ast_channel *chan, void *data )
{
    struct ast_module_user *u = NULL;
    MP4FileHandle mp4;
    struct mp4play *player;
    //const char *type = NULL;

    char *parse;
    int numberofDigits = -1;
    char *varName = NULL;
    char *stopChars = NULL;
    char dtmfBuffer[MAX_DTMF_BUFFER_SIZE];
    struct ast_flags opts = { 0, };
    char *opt_args[OPT_ARG_ARRAY_SIZE];

    char cformat1[_STR_CODEC_SIZE] = { 0 };
    char cformat2[_STR_CODEC_SIZE] = { 0 };
    struct timeval tv, tvs;
    int ms = 0;
    int res;

    //struct ast_frame *f = (struct ast_frame *) buffer;
    AST_DECLARE_APP_ARGS( args, AST_APP_ARG( filename ); AST_APP_ARG( options ););

    /* Check for data */
    if( !data || ast_strlen_zero( data ) )
    {
        ast_log( LOG_WARNING, "mp4play requires an argument (filename)\n" );
        return -1;
    }

/* Reset dtmf buffer */
    memset( dtmfBuffer, 0, MAX_DTMF_BUFFER_SIZE );

    /* Lock module */
    u = ast_module_user_add( chan );

    /* Duplicate input */
    parse = ast_strdup( data );

    /* Get input data */
    AST_STANDARD_APP_ARGS( args, parse );

    /* Parse input data */
    if( !ast_strlen_zero( args.options ) &&
        ast_app_parse_options( mp4play_exec_options, &opts, opt_args, args.options ) )
    {
        ast_log( LOG_WARNING, "mp4play: cannot parse options\n" );
        res = -1;
        goto clean;
    }

    /* Check filename */
    if( ast_strlen_zero( args.filename ) )
    {
        ast_log( LOG_WARNING, "mp4play requires an argument (filename)\n" );
        res = -1;
        goto clean;
    }

    ast_verbose( VERBOSE_PREFIX_3 "MP4Play [%s].\n", args.filename );

    /* If we have DTMF number of digits options chek it */
    if( ast_test_flag( &opts, OPT_NOOFDTMF ) && !ast_strlen_zero( opt_args[OPT_ARG_NOOFDTMF] ) )
    {

/* Get number of digits to wait for */
        numberofDigits = atoi( opt_args[OPT_ARG_NOOFDTMF] );

        /* Check valid number */
        if( numberofDigits < 0 )
        {
            ast_log( LOG_WARNING, "mp4play does not accept n(%s), hanging up.\n", opt_args[OPT_ARG_NOOFDTMF] );
            res = -1;
            goto clean;
        }

        /* Check valid sizei */
        if( numberofDigits > MAX_DTMF_BUFFER_SIZE - 1 )
        {
            numberofDigits = MAX_DTMF_BUFFER_SIZE - 1;
            ast_log( LOG_WARNING, "mp4play does not accept n(%s), buffer is too short cutting to %d .\n", opt_args[OPT_ARG_NOOFDTMF], MAX_DTMF_BUFFER_SIZE - 1 );
        }

        if( option_verbose > 2 )
            ast_verbose( VERBOSE_PREFIX_3 "Setting number of digits to %d seconds.\n", numberofDigits );
    }

    /* If we have DTMF set variable otpion chekc it */
    if( ast_test_flag( &opts, OPT_DFTMINTOVAR ) && !ast_strlen_zero( opt_args[OPT_ARG_DFTMINTOVAR] ) )
    {

/* Get variable name */
        varName = opt_args[OPT_ARG_DFTMINTOVAR];

        if( option_verbose > 2 )
            ast_verbose( VERBOSE_PREFIX_3 "Setting variable name to %s .\n", varName );
    }

    /* If we have DTMF stop digit optiont check it */
    if( ast_test_flag( &opts, OPT_STOPDTMF ) && !ast_strlen_zero( opt_args[OPT_ARG_STOPDTMF] ) )
    {

/* Get stop digits */
        stopChars = opt_args[OPT_ARG_STOPDTMF];

        if( option_verbose > 2 )
            ast_verbose( VERBOSE_PREFIX_3 "Stop chars are %s.\n", stopChars );
    }


    if( args.filename[0] != '/' )
    {
        struct stat s;
        /* Use standard asterisk sound directory */
        sprintf( cformat1, "%s/sounds/%s/%s",
            ast_config_AST_DATA_DIR, chan->language, args.filename );

        if( stat( cformat1, &s ) < 0 )
        {
        // Could not open file. Try without language
            sprintf( cformat1, "%s/sounds/%s",
                ast_config_AST_DATA_DIR, args.filename );
        }
    }
    else
    {
        /* Use absolute path */
        strcpy( cformat1, args.filename );
    }

    ast_verbose( VERBOSE_PREFIX_3 "MP4Play [%s].\n", cformat1 );
    mp4 = MP4Read( cformat1 );

    /* If not valid */
    if( mp4 == MP4_INVALID_FILE_HANDLE )
    {
        ast_log( LOG_WARNING, "mp4play: failed to open file %s.\n", cformat1 );
        /* exit */
        res = -1;
        goto clean;
    }

    player = Mp4PlayerCreate( chan, mp4, true, 0 );
    if( player == NULL )
    {
        ast_log( LOG_WARNING, "mp4play: failed to create MP4 player for file %s.\n", args.filename );
        res = -1;
        goto end;
    }

    ast_log( LOG_DEBUG, "Native formats:%s , Channel capabilites ( videocaps.cap ):%s\n",
        ast_getformatname_multiple( cformat1, _STR_CODEC_SIZE, chan->nativeformats ),
        ast_getformatname_multiple( cformat2, _STR_CODEC_SIZE, chan->channelcaps.cap ) );


    tv = ast_tvnow();

    while( ms >= 0 )
    {
        ms = Mp4PlayerPlayNextFrame( chan, player );

        if( ms < 0 )
        {
            if( ms == -1 )
            {
                ast_verbose( VERBOSE_PREFIX_3 " MP4Play [%s] stopped (end of file)\n", args.filename );
                res = 0;
            }
            else
            {
                ast_log( LOG_ERROR, "mp4play: failed to play file %s. Error code=%d.\n",
                    args.filename, ms );
                res = -1;
            }
            break;
        }

        if( ms > 1000 ) ast_log( LOG_NOTICE, "mp4_play: next frame to be streamed in %d ms.\n", ms );
    // Wait x ms 
        while( ms > 0 )
        {
            ms = ast_waitfor( chan, ms );
            tvs = ast_tvnow();
            //
            // compute global timestamp to check if we are late
            // proctime = ast_tvdiff_ms(tvs, tv);

            if( ms > 0 )
            {
                int proctime;
                struct ast_frame *f = ast_read( chan );

                if( f == NULL )
                {
                    ast_verbose( VERBOSE_PREFIX_3 " MP4Play [%s] stopped (hangup)\n", args.filename );
                    res = 0;
                    goto end;
                }

                res = mp4_play_process_frame( chan, f, dtmfBuffer, stopChars, numberofDigits, varName );
                if( res < 0 )
                {
                    goto end;
                }

                proctime = ast_tvdiff_ms( ast_tvnow(), tvs );
                ms -= proctime;
            }
            else if( ms < 0 )
            {
                ast_verbose( VERBOSE_PREFIX_3 " MP4Play [%s] stopped (error)\n", args.filename );
                res = 0;
            }
        }
    }

end:
    if( player != NULL ) Mp4PlayerDestroy( player );
    /* Close file */
    MP4Close( mp4, 0 );

clean:
    /* Unlock module*/
    ast_module_user_remove( u );

    /* Free datra*/
    free( parse );

    /* Exit */
    return res;
}



static int record_frames( struct AstFb *recQueues[], struct ast_channel *chan, struct mp4rec *recorder, int loopback, int flush )
{
    if( recorder )
    {
        struct ast_frame *fr;
        int m;
        int i;
        int ret;


        for( m = 0; m < 3; m++ )
        {

            if( recQueues[m] )
            {

                //ast_log(LOG_DEBUG, "queue %d for chan %s has %d packets.\n", m,
                //		 chan->name, AstFbLength(recQueues[m]) );
                for( i = 0; i < 200; i++ )
                {
                    if( flush ) AstFbUnblock( recQueues[m] );
                    fr = AstFbGetFrame( recQueues[m] );
                    if( fr )
                    {
                        if( m == 1 && AstFbGetLoss( recQueues[m] ) > 0 )
                        {
                            ast_log( LOG_NOTICE, "mp4save:  %s lost video packet.\n", chan->name );
                            ast_indicate( chan, AST_CONTROL_VIDUPDATE );
                        }

                        ret = Mp4RecorderFrame( recorder, fr );
                        if( ret == -333 )
                            ast_indicate( chan, AST_CONTROL_VIDUPDATE );
                        else if( ret < 0 && ret != -4 )
                            ast_log( LOG_DEBUG, "mp4save: Failed to record frame, err=%d.\n", ret );

                        // Send back video frame if requested
                        if( loopback && m == 1 && Mp4RecorderHasVideoStarted( recorder ) )
                        {
                            /* -- ast_write() destroys the frame -- */
                            ast_write( chan, fr );
                        }
                        else
                        {
                            ast_frfree( fr );
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
    }
    return 0;
}



static int mp4_save( struct ast_channel *chan, void *data )
{
    struct ast_module_user *u = NULL;
    struct ast_frame *f = NULL;
    char *params = NULL;
    int maxduration = 1000 * 1200;		/* max duration of recording in milliseconds - 20 mins */
    int remainingduration = maxduration;
    int waitres;
    char stopDtmfs[20] = "#";
    struct mp4rec *recorder;
    char metadata[100];
    MP4FileHandle mp4;
    char traceFilename[200];

    /*  whether we send back the video packets to the caller */
    int videoLoopback = 0;

    /*  whether we wait for video I-frame to start recording */
    int waitVideo = 0;

    /* whether we record chat in a separate text file */
    int saveInTxtFile = 1;

    /*  Recording is on man! */
    int onrecord = 1;
    int textfile = -1;

    struct AstFb *audioInQueue;
    struct AstFb *videoInQueue;
    struct AstFb *textInQueue;

    struct AstFb *queueTab[3];
    //struct AstFb * queueTab2[3];


    /* Check for file */
    if( !data ) return -1;


    /* Check for params */
    params = strchr( data, '|' );

    /* If there are params */
    if( params )
    {
        /* Remove from file name */
        *params = 0;

        /* Increase pointer */
        params++;

        /* Check video loopback */
        if( strchr( params, 'v' ) )
        {
            /* Enable video loopback */
            videoLoopback = 1;
        }

        /* Check video waiting */
        if( strchr( params, 'V' ) )
        {
            /* Enable video loopback & waiting*/
            videoLoopback = 1;
            waitVideo = 1;
        }

        /* Check video waiting */
        if( strchr( params, 'T' ) )
        {
            saveInTxtFile = 0;
        }

        int i, j = strlen( stopDtmfs );
        for( i = 0; i < strlen( params ); i++ )
        {

            if( (params[i] >= '0' && params[i] <= '9')
                ||
                params[i] == '*' )
            {
                stopDtmfs[j++] = params[i];
                stopDtmfs[j] = '\0';
            }
        }
    }

    ast_verbose( VERBOSE_PREFIX_3
        "MP4Save [%s], maxduration=%ds\n",
        (char *)data, maxduration / 1000 );

 /* Lock module */
    u = ast_module_user_add( chan );

    /* Create mp4 file */
    mp4 = MP4Create( (char *)data, 0 );

    /* If failed */
    if( mp4 == MP4_INVALID_FILE_HANDLE )
    {
        ast_log( LOG_ERROR, "Fail to create MP4 file %s.\n", (char *)data );
        goto mp4_save_cleanup;
    }

    if( (chan->nativeformats & AST_FORMAT_TEXT_MASK) != 0 && saveInTxtFile != 0 )
    {
        char textFileName[200];

        strcpy( textFileName, (char *)data );
        strcat( textFileName, ".txt" );

        textfile = open( textFileName, O_CREAT | O_WRONLY );
        if( textfile == -1 )
        {
            ast_log( LOG_WARNING, "Fail to create text file %s.\n", textFileName );
        }
        else
        {
            ast_log( LOG_DEBUG, "Created text file %s. fd = %d\n", textFileName, textfile );
        }
    }
    else
    {
        ast_log( LOG_DEBUG, "No need to create text file\n" );
    }


    time_t now;
    struct tm *tmvalue;
    MP4Tags *tags = MP4TagsAlloc();

    time( &now );
    tmvalue = localtime( &now );
    MP4TagsSetEncodingTool( tags, "MP4Save asterisk application" );
    MP4TagsSetArtist( tags, chan->cid.cid_name );

    sprintf( metadata, "%04d/%02d/%02d %02d:%02d:%02d",
        tmvalue->tm_year + 1900, tmvalue->tm_mon + 1, tmvalue->tm_mday,
        tmvalue->tm_hour, tmvalue->tm_min, tmvalue->tm_sec );

    MP4TagsSetReleaseDate( tags, metadata );
    MP4TagsStore( tags, mp4 );

    recorder = Mp4RecorderCreate( chan, mp4, waitVideo, "h264@vga", chan->cid.cid_name, textfile );

    if( recorder == NULL )
    {
        ast_log( LOG_ERROR, "Fail to create MP4 recorder. Exiting\n" );
        goto mp4_save_cleanup;
    }

    Mp4RecorderEnableVideoPrologue( recorder, false );

#ifdef VIDEOCAPS
    int oldnative = chan->nativeformats;
    if( chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK )
    {
        chan->nativeformats = chan->channelcaps.cap;
        ast_log( LOG_WARNING, "mp4_save: already received audio format %08x.\n",
            chan->channelcaps.cap & AST_FORMAT_AUDIO_MASK );
    }
    else
    {
        ast_log( LOG_WARNING, "mp4_save: using original native formats.\n" );
    }
#endif


    int length = strlen( data );
    if( !strcmp( data + length - 4, ".3gp" ) )
    {
        if( ast_set_read_format( chan, AST_FORMAT_AMRNB ) )
            ast_log( LOG_WARNING, "mp4_save: Unable to set read format to AMRNB!\n" );
    }
    else
    {
        if( ast_set_read_format( chan, AST_FORMAT_ULAW ) )
            ast_log( LOG_WARNING, "mp4_save: Unable to set read format to ALAW!\n" );
    }

#ifdef VIDEOCAPS
    chan->nativeformats = oldnative;
#endif

    /* no max duration */
    if( maxduration <= 0 ) remainingduration = -1;

    /* Send video update */
    ast_indicate( chan, AST_CONTROL_VIDUPDATE );

    audioInQueue = AstFbCreate( 60, 0, 0 );
    videoInQueue = AstFbCreate( 60, 0, 0 );
    textInQueue = AstFbCreate( 40, 0, 0 );

    queueTab[0] = audioInQueue;
    queueTab[1] = videoInQueue;
    queueTab[2] = textInQueue;

    sprintf( traceFilename, "/var/log/asterisk/mp4save-videojb-%p.log", videoInQueue );
    //AstFbTrace(videoInQueue, traceFilename);

    while( onrecord )
    {
        waitres = ast_waitfor( chan, remainingduration );
        if( waitres < 0 )
        {
            /* hangup or error - trace ?*/
            onrecord = 0;
        }

        if( maxduration > 0 )
        {
            if( waitres == 0 )
            {
                ast_log( LOG_NOTICE, "Max recording duration %d seconds elapsed. Recording will stop.\n",
                    maxduration / 1000 );
                onrecord = 0;
            }
            else
            {
                remainingduration = waitres;
            }
        }

        /* Read frame from channel */
        f = ast_read( chan );

        /* if it's null */
        if( f == NULL )
        {
            ast_log( LOG_DEBUG, "null frame: hangup.\n" );
            onrecord = 0;
            break;
        }

        /* --- post all media frames in a reorder buffer --- */
        switch( f->frametype )
        {
            case AST_FRAME_VOICE:
                AstFbAddFrame( audioInQueue, f );
                break;

            case AST_FRAME_VIDEO:
                //ast_log(LOG_DEBUG, "video frame: ok.\n");
                AstFbAddFrame( videoInQueue, f );
                break;

            case AST_FRAME_TEXT:
                AstFbAddFrame( textInQueue, f );
                break;

            case AST_FRAME_DTMF:
                if( strchr( stopDtmfs, f->subclass ) )
                {
                    ast_log( LOG_NOTICE,
                        "mp4_save: recording stopping because DTMF %c was pressed.\n",
                        (char)f->subclass );
                    onrecord = 0;
                }
                break;

            default:
                break;
        }

        /* -- now poll all the queues and record -- */
        //waitres = AstFbWaitMulti(queueTab, 3, 500, queueTab2);
        record_frames( queueTab, chan, recorder, videoLoopback, 0 );
    }

mp4_save_cleanup:

    /* flush queues in file */
    record_frames( queueTab, chan, recorder, videoLoopback, 1 );

    /* destroy resources */
    waitVideo = -1;
    if( recorder )
    {
        waitVideo = Mp4RecorderHasVideoStarted( recorder );
        Mp4RecorderDestroy( recorder );
    }

    if( audioInQueue ) AstFbDestroy( audioInQueue );
    if( videoInQueue ) AstFbDestroy( videoInQueue );
    if( textInQueue ) AstFbDestroy( textInQueue );

    MP4TagsFree( tags );
    /* Close file */
    MP4Close( mp4, 0 );

    /* Remove file if video had not started */
    if( waitVideo == 0 )
    {
        ast_verbose( VERBOSE_PREFIX_3 "Removed recorde MP4 file %s because no intraframe was received.\n", (char *)data );
        unlink( (char *)data );
    }

    if( textfile >= 0 )
    {
        ast_log( LOG_DEBUG, "Closed text file fd %d\n", textfile );
        close( textfile );
    }
    if( option_verbose > 2 )
    {
        char *inf = MP4FileInfo( (char *)data, MP4_INVALID_TRACK_ID );
        if( inf )
        {
            ast_verbose( VERBOSE_PREFIX_3 "Information about the recorded MP4 file: %s\n%s\n", (char *)data, inf );
            free( inf );
        }
    }

    /* Unlock module*/
    ast_module_user_remove( u );

    //Success
    return 0;
}



static int unload_module( void )
{
    int res;

    res = ast_unregister_application( app_play );
    res &= ast_unregister_application( app_save );

    ast_module_user_hangup_all();

    return res;

}

static int load_module( void )
{
    int res;

    res = ast_register_application( app_save, mp4_save, syn_save, des_save );
    res &= ast_register_application( app_play, mp4_play, syn_play, des_play );
    RedirectLogToAsterisk( 2 );
    return 0;
}

AST_MODULE_INFO_STANDARD( ASTERISK_GPL_KEY, "MP4 applications" );

