#include <asterisk/frame.h>
#include <asterisk/channel.h>
#include "medkit/astcpp.h"
#include "astmedkit/mp4format.h"
#include "astmedkit/frameutils.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"
#include "medkit/audiosilence.h"


mp4recorder::mp4recorder(void * ctxdata, MP4FileHandle mp4, bool waitVideo)
{
    this->ctxdata = ctxdata;
    this->mp4 = mp4;
    textSeqNo = 0xFFFF;
    vtc = NULL;
    this->waitVideo = waitVideo;
    Log("mp4recorder: created with waitVideo %s.\n", waitVideo ? "enabled" : "disabled" );
    audioencoder = NULL;
    depak = NULL;
    SetParticipantName( "participant" );
    gettimeofday(&firstframets,NULL);
    initialDelay = 0;
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
	mediatracks[i] = NULL;
    }
}

mp4recorder::~mp4recorder()
{
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
        if ( mediatracks[i] ) delete mediatracks[i];
    }

    if (audioencoder) delete audioencoder;
    if (depak) delete depak;
}

int mp4recorder::AddTrack(AudioCodec::Type codec, DWORD samplerate, const char * trackName)
{
    if ( mediatracks[MP4_AUDIO_TRACK] == NULL )
    {
	mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack(mp4, initialDelay);
	if ( mediatracks[MP4_AUDIO_TRACK] != NULL )
	{
	    mediatracks[MP4_AUDIO_TRACK]->Create( trackName, (int) codec, samplerate );
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}

int mp4recorder::AddTrack(VideoCodec::Type codec, DWORD width, DWORD height, DWORD bitrate, const char * trackName, bool secondary )
{
    int trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
    
    if ( mediatracks[trackidx] == NULL )
    {
        Mp4VideoTrack * vtr = new Mp4VideoTrack(mp4, initialDelay);
	mediatracks[trackidx] = vtr;
	if ( mediatracks[trackidx] != NULL )
	{
	    vtr->SetSize(width, height);
	    vtr->Create( trackName, codec, bitrate );
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}

int mp4recorder::AddTrack(TextCodec::Type codec, const char * trackName, int textfile)
{
    if ( mediatracks[MP4_TEXT_TRACK] == NULL )
    {
	mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack(mp4, textfile, initialDelay);
	if ( mediatracks[MP4_TEXT_TRACK] != NULL )
	{
	    char introsubtitle[200];
	    
	    mediatracks[MP4_TEXT_TRACK]->Create( trackName, codec, 1000 );
	    sprintf(introsubtitle, "[%s]\n", trackName);
	    TextFrame tf(false);
	    
	    tf.SetMedia((const uint8_t*) introsubtitle, strlen(introsubtitle) );
	    
	    mediatracks[MP4_TEXT_TRACK]->ProcessFrame(&tf);
	    
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}

int mp4recorder::IsVideoStarted()
{
    if ( mediatracks[MP4_VIDEO_TRACK] != NULL )
    {
	return  ( ( (Mp4VideoTrack *) mediatracks[MP4_VIDEO_TRACK])->IsVideoStarted() ) ? 1 : 0;
    }
    return -1;
}

int mp4recorder::ProcessFrame( const MediaFrame * f, bool secondary )
{
    int trackidx;

    switch ( f->GetType() )
    {
        case MediaFrame::Audio:
	    if ( mediatracks[MP4_AUDIO_TRACK] )
	    {
		if (waitVideo) 
		    return 0;
		    
		if ( mediatracks[MP4_AUDIO_TRACK]->IsEmpty() )
		{
		    // adjust initial delay
		    mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		}
	        return mediatracks[MP4_AUDIO_TRACK]->ProcessFrame(f);
	    }
	    else
	    {
	        /* auto create audio track if needed */
		AudioFrame * f2 = (AudioFrame *) f;
	        AddTrack( f2->GetCodec(), f2->GetRate(), partName );
		
		if ( mediatracks[MP4_AUDIO_TRACK] )
		{
		    mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		    if (waitVideo)  return 0;
		    return mediatracks[MP4_AUDIO_TRACK]->ProcessFrame(f);
		}
		else
		    return -3;
	    }
	    break;
	    
	case MediaFrame::Video:
	    trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
	    if ( mediatracks[trackidx] )
	    {
		if ( mediatracks[trackidx]->IsEmpty() )
		{
		    // adjust initial delay
		    mediatracks[trackidx]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		}
	    
	        int ret = mediatracks[trackidx]->ProcessFrame(f);
		if ( waitVideo && ( (Mp4VideoTrack *) mediatracks[trackidx])->IsVideoStarted() )
		{
		    Log("-mp4recorder: video has started after %lu ms.\n", getDifTime(&firstframets)/1000 );
		    waitVideo = false;
		}
		return ret;
	    }
	    else
	    {
		return -3;
	    }
	    break;
	    
	case MediaFrame::Text:
	    if ( mediatracks[MP4_TEXT_TRACK] )
	    {
		if ( mediatracks[MP4_TEXT_TRACK]->IsEmpty() )
		{
		    // adjust initial delay
		    mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		}
	        if (waitVideo) return 0;
	        return mediatracks[MP4_TEXT_TRACK]->ProcessFrame(f);
	    }
	    else
	    {
	        /* auto create text track if needed */
		const char * n = &partName[0];
	        AddTrack( TextCodec::T140, n , 0);
		
		if ( mediatracks[MP4_TEXT_TRACK] )
		{
		    mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		    if (waitVideo)  return 0;
		    if ( mediatracks[MP4_TEXT_TRACK]->IsEmpty() && initialDelay > 0 )
		    {
		        // adjust initial delay
		        mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
		    }

		    return mediatracks[MP4_TEXT_TRACK]->ProcessFrame(f);
		}
		else
		    return -3;
	    
		return -3;
	    }
	    break;
	
	default:
	    break;
    }
}

bool AstFormatToCodec( int format, AudioCodec::Type & codec )
{
    switch ( ((unsigned int) format) & 0xFFFFFFFE )
    {
        case AST_FORMAT_ULAW:
	    codec = AudioCodec::PCMU;
	    break;
	    
	case AST_FORMAT_ALAW:
	    codec = AudioCodec::PCMA;
	    break;
	    
	case AST_FORMAT_AMRNB:
	    codec = AudioCodec::AMR;
	    break;
	
	default:
	    Log("AstFormatToCodec: unsupported ast_format %08lu.\n",
		((unsigned int) format) & 0xFFFFFFFE );
	    return false;
    }
    return true;
}
	
bool AstFormatToCodec( int format, VideoCodec::Type & codec )
{
    switch ( ((unsigned int) format) & 0xFFFFFFFE )
    {
        case AST_FORMAT_H263:
	    codec = VideoCodec::H263_1996;
	    break;
	    
	case AST_FORMAT_H263_PLUS:
	    codec = VideoCodec::H263_1998;
	    break;
	    
	case AST_FORMAT_H264:
	    codec = VideoCodec::H264;
	    break;
	
	default:
	    return false;
    }
    return true;
}

void  mp4recorder::SetInitialDelay(unsigned long delay)
{
    initialDelay = delay;

    for (int i =0; i < (MP4_TEXT_TRACK + 1); i++)
    {
        if ( mediatracks[i] )  mediatracks[i]->SetInitialDelay(delay);
    }

}
	
int mp4recorder::ProcessFrame(struct ast_frame * f, bool secondary )
{
    if (f != NULL)
    {
	int ret;
	
        switch(f->frametype)
	{
	    case AST_FRAME_VOICE:
	    {	
	        AudioCodec::Type acodec = AudioCodec::PCMU;
		AudioFrame af( acodec, 8000, false );
		
		af.SetTimestamp( f->ts );
		
		if ( f->subclass == AST_FORMAT_SLINEAR )
		{
		    // If audio received is SLINEAR - transcode
		    int outLen = sizeof(audioBuff);
		    acodec = AudioCodec::PCMU;
		    
		    if (audioencoder == NULL)
			audioencoder = AudioCodecFactory::CreateEncoder(AudioCodec::PCMU);
   
		    outLen = audioencoder->Encode( (SWORD*) AST_FRAME_GET_BUFFER(f), f->datalen / 2, 
						    audioBuff, outLen );
		    if ( outLen > 0 ) 
		        af.SetMedia( audioBuff, outLen );
		    else
			return 0;
		    
		}
		else if ( AstFormatToCodec( f->subclass, acodec) )
		{
		     af.SetCodec( acodec );
		     af.SetMedia( AST_FRAME_GET_BUFFER(f), f->datalen );
		}
		else
		{
		    /* unsupported codec */
		    return -4;
		}		
		return ProcessFrame( &af );
	    }
	    		
	    case AST_FRAME_VIDEO:
	    {
	        VideoCodec::Type vcodec;
		int ret;
		bool ismark = ( f->subclass & 0x01 ) != 0;
		if ( AstFormatToCodec( f->subclass, vcodec ) )
		{
		    switch(vcodec)
		    {
		        case VideoCodec::H264:
			    {
			        MediaFrame * vfh264;
			        if (depak == NULL)
			        {
			            depak = new H264Depacketizer();
			        }
				
				// Accumulate NALs into the same frame until mark
			        vfh264 = depak->AddPayload(AST_FRAME_GET_BUFFER(f), f->datalen,  ismark); 
				
				// Do the same in case of lost frame
				if (ismark)
				{
				    if ( ast_test_flag( f, AST_FRFLAG_HAS_TIMING_INFO) )
				        depak->SetTimestamp( f->ts );
				    else
				        depak->SetTimestamp( getDifTime(&firstframets)/1000 );
				   
				    //Log("H.264 - got mark. frame ts = %ld.\n", f->ts );
				    ret = ProcessFrame( vfh264 );
				    depak->ResetFrame();
				}
				else
				{
				    // no mark ? will be processed later
				    return 1;    
				}    
			    }
			    break;
			    
			default:
			    {
				VideoFrame vf(vcodec, f->datalen, false);
				if ( ast_test_flag( f, AST_FRFLAG_HAS_TIMING_INFO) )
				        vf.SetTimestamp( f->ts );
				    else
				        vf.SetTimestamp( getDifTime(&firstframets)/1000 );

				vf.SetMedia( AST_FRAME_GET_BUFFER(f), f->datalen );
				vf.AddRtpPacket( 0, f->datalen, NULL, 0, ismark);
				ret = ProcessFrame( &vf );
			    }
			    break;
		
		    }
		    
		    if ( ret == -1 && vtc != NULL)
		    {
				/* we need to transcode */
				return VideoTranscoderProcessFrame( vtc, f );
		    }
		    return ret;
		}
		else
		{
		    return -4;
		}
	    }		
		
	    case AST_FRAME_TEXT:
	    {
		DWORD lost = 0, text_ts;
	        TextCodec::Type tcodec;
		TextFrame tf( true );

		//If not first
		if (textSeqNo != 0xFFFF)
			//Calculate losts
			lost = f->seqno - textSeqNo-1;

		//Update last sequence number
		textSeqNo = f->seqno;
		//Log("text frame seqno %d, lost %d\n", f->seqno, lost);

		// Generate timing INFO
		text_ts = getDifTime(&firstframets)/1000 ;

		if ( f->subclass == AST_FORMAT_RED )
		{
		    // parse RED to recover lost packets
		    RTPRedundantPayload red( AST_FRAME_GET_BUFFER(f), f->datalen );
		    
		    if (lost > 0 && red.GetRedundantCount() > 0)
		    {
		        if ( lost > red.GetRedundantCount()  )
			{
			    /* cas ou l'on a perdu + de paquet de le niv de red. On ne fait rien */
			    lost = red.GetRedundantCount();
			}
			
			//Fore each recovered packet
			for (int i=red.GetRedundantCount()-lost;i<red.GetRedundantCount();i++)
			{
				//Create frame from recovered data - check timestamps ...
				tf.SetTimestamp( text_ts - red.GetRedundantTimestampOffset(i) );
				tf.SetMedia( red.GetRedundantPayloadData(i),red.GetRedundantPayloadSize(i) );
				ProcessFrame ( &tf );
			}
		    }

		    //char ttr[800];
		    //
		    // strncpy( ttr, (const char *) red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
		    //ttr[  red.GetPrimaryPayloadSize() ] = '\0';
		    //Log("Primary data [%s] len %d.\n", ttr, red.GetPrimaryPayloadSize() );
		    tf.SetFrame( text_ts, red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
		}
		else /* assume plain text */
		{
		    tf.SetFrame( text_ts, (const BYTE *) AST_FRAME_GET_BUFFER(f), f->datalen );
		}
		
		return ProcessFrame( &tf );
	    }
	}
    }
}

mp4player::mp4player(void * ctxdata, MP4FileHandle mp4)
{
	this->mp4 = mp4;
	this->ctxdata = ctxdata;
	mediatracks[MP4_AUDIO_TRACK] = NULL;
	mediatracks[MP4_VIDEO_TRACK] = NULL;
	mediatracks[MP4_VIDEODOC_TRACK] = NULL
	mediatracks[MP4_TEXT_TRACK]  = NULL;
	redenc = NULL;
	
	audioNext = 0;
	videoNext = 0;
	video2Next = 0;
	textNext = 0;
}


int mp4player::OpenTrack(AudioCodec::Type outputCodecs[], unsigned int nbCodecs, AudioCodec::Type prefCodec, bool cantranscode )
{
    if (nbCodecs > 0)
    {
	MP4TrackId hintId = NO_CODEC ;
	MP4TrackId trackId = -1;
	MP4TrackId lastHintMatch = -1;
	MP4TrackId lastTrackMatch = -1;
	int idxTrack = 0;
	
	if (mediatracks[MP4_AUDIO_TRACK] != NULL)
	{
	    Error("Audio track is already open.\n");
	    return 0;
	}
	
	hintId = MP4FindTrackId(mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0);
	while (hintId != MP4_INVALID_TRACK_ID)
	{
	    const char* nm = MP4GetTrackMediaDataName(mp4,hintId);
	    Debug("found hint track %d (%s)\n", hintId,nm?nm: "null");
	    
	    /* Get associated track */
	    trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
		
	    /* Check it's good */
	    if (trackId != MP4_INVALID_TRACK_ID)
	    {
		/* Get type */
		type = MP4GetTrackType(mp4, trackId);

		if (type != NULL && strcmp(type, MP4_AUDIO_TRACK_TYPE) == 0)
		{
		    char *name;
		    AudioCodec::Type c;
		    
		    MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);
		    
		    if (name == NULL)
		    {
			c = AudioCodec::AMR;
		    }
		    else 
		    {
			if ( ! AudioCodec::GetCodecFor(name, c) )
			{
			    Log("Unsupported audio codec %d for hint track ID %d.\n", name, hintId);
			    goto audio_track_loop;
			}
		    }
		    
		    if ( c == prefCodec )
		    {
			// This is the preffered codec !
			// use it and stop here
			
			lastTrackMatch = trackId;
			lastHintMatch = hintId;
			break;
		    }    
		    
		    if ( lastTrackMatch < 0)
		    {
			for (int i=0; i<nbCodecs; i++)
			{
			    if ( outputCodecs[i] == c )
			    {
				lastTrackMatch = trackId;
				lastHintMatch = hintId;
			    }
			}
		    }
		}
		
	    }
	    else
	    {
		Log("No media track associated with hint track ID %d.\n", hintId);
	    }
	    
audio_track_loop:
	    idxTrack++;
	    hintId = MP4FindTrackId(mp4, idxTrack , MP4_HINT_TRACK_TYPE, 0);
	}

	if ( lastTrackMatch >= 0)
	{
	     mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack(mp4, lastTrackMatch, lastHintMatch);
	     return 1;
	}
	else
	{
	     return 0;
	}
    }
}

int mp4player::OpenTrack(VideoCodec::Type outputCodecs[], unsigned int nbCodecs, VideoCodec::Type prefCodec, bool cantranscode, bool secondary )
{
    if (nbCodecs > 0)
    {
	MP4TrackId hintId = NO_CODEC ;
	MP4TrackId trackId = -1;
	MP4TrackId lastHintMatch = -1;
	MP4TrackId lastTrackMatch = -1;
	int idxTrack = 0;
	
	if (mediatracks[MP4_VIDEO_TRACK] != NULL)
	{
	    Error("Video track is already open.\n");
	    return 0;
	}
	
	hintId = MP4FindTrackId(mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0);
	while (hintId != MP4_INVALID_TRACK_ID)
	{
	    const char* nm = MP4GetTrackMediaDataName(mp4,hintId);
	    Debug("found hint track %d (%s)\n", hintId,nm?nm: "null");
	    
	    /* Get associated track */
	    trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
		
	    /* Check it's good */
	    if (trackId != MP4_INVALID_TRACK_ID)
	    {
		/* Get type */
		type = MP4GetTrackType(mp4, trackId);

		if (type != NULL && strcmp(type, MP4_VIDEO_TRACK_TYPE) == 0)
		{
		    char *name;
		    AudioCodec::Type c;
		    
		    MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);
		    
		    if (name == NULL)
		    {
			c = AudioCodec::AMR;
		    }
		    else 
		    {
			if ( ! VideoCodec::GetCodecFor(name, c) )
			{
			    Log("Unsupported video codec %d for hint track ID %d.\n", name, hintId);
			    goto video_track_loop;
			}
		    }
		    
		    if ( c == prefCodec )
		    {
			// This is the preffered codec !
			// use it and stop here
			
			lastTrackMatch = trackId;
			lastHintMatch = hintId;
			break;
		    }    
		    
		    if ( lastTrackMatch < 0)
		    {
			for (int i=0; i<nbCodecs; i++)
			{
			    if ( outputCodecs[i] == c )
			    {
				lastTrackMatch = trackId;
				lastHintMatch = hintId;
			    }
			}
		    }
		}
		
	    }
	    else
	    {
		Log("No media track associated with hint track ID %d.\n", hintId);
	    }
	    
vided_track_loop:
	    idxTrack++;
	    hintId = MP4FindTrackId(mp4, idxTrack , MP4_HINT_TRACK_TYPE, 0);
	}

	if ( lastTrackMatch >= 0)
	{
	     if (secondary)
		mediatracks[MP4_VIDEO_TRACK] = new Mp4AudioTrack(mp4, lastTrackMatch, lastHintMatch);
	     else
		mediatracks[MP4_VIDEODOC_TRACK] = new Mp4AudioTrack(mp4, lastTrackMatch, lastHintMatch);
	     return 1;
	}
	else
	{
	     return 0;
	}
    }
}

int mp4player::OpenTrack(TextCodec::Type c, BYTE pt, int rendering)
{
    if (mediatracks[MP4_TEXT_TRACK] != NULL)
    {
        Error("Audio track is already open.\n");
        return 0;
    }

    if ( c == TextCodec::T140RED)
    {
	redenc = new RTPRedundantEncoder(pt)
    }
    
    MP4TrackId textId = MP4FindTrackId(mp4, 0, MP4_TEXT_TRACK_TYPE, 0);
    
    if (textId != MP4_INVALID_TRACK_ID)
    {
	mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack(mp4, textId);
	Log("Opened text track ID %d.\n", textId);
	return 1;
    }
    else
    {
	return Error("Could not find any text track in this file.\n");
    }
}

bool mp4player::EOF()
{
    if ( mediatracks[MP4_AUDIO_TRACK] && audioNext != MP4_INVALID_TIMESTAMP )
	return false;

    if ( mediatracks[MP4_VIDEO_TRACK] && videoNext != MP4_INVALID_TIMESTAMP )
	return false;

    if ( mediatracks[MP4_TEXT_TRACK] && textNext != MP4_INVALID_TIMESTAMP )
	return false;

    return true;
}
MediaFrame * GetNextFrame( int & errcode, unsigned long & waittime )
{    
    timeval tv ;
    timespec ts;

    
    

}
/* ---- callbeck used for video transcoding --- */

void Mp4RecoderVideoCb(void * ctxdata, int outputcodec, const char *output, size_t outputlen)
{
    mp4recorder * r2 = (mp4recorder *) ctxdata;
    VideoFrame vf( (VideoCodec::Type) outputcodec, 2000, false);
    
    if (r2)
    {
        vf.SetMedia( (uint8_t *) output, outputlen);
	// add timestamp
	r2->ProcessFrame(&vf);
    }
}    

struct mp4rec * Mp4RecorderCreate(struct ast_channel * chan, MP4FileHandle mp4, bool waitVideo, 
				  const char * videoformat, const char * partName, int textfile)
{
    if ( (chan->nativeformats & AST_FORMAT_VIDEO_MASK) == 0 )
    {
        waitVideo = false;
	Log("-mp4recorder: disable video waiting as char %s does not support video.\n",
	    chan->name);
    }
    
    mp4recorder * r = new mp4recorder(chan, mp4, waitVideo);
    if ( partName == NULL ) partName = chan->cid.cid_name ? chan->cid.cid_name: "unknown";
    if ( r != NULL)
    {
        r->SetParticipantName( partName );
        if ( videoformat != NULL && strlen(videoformat) > 0 && (chan->nativeformats & AST_FORMAT_VIDEO_MASK) != 0 )
        {
            // Hardcoded for now
	    r->AddTrack(VideoCodec::H264, 640, 480, 256, partName, false );
	
        }
	
	if ( chan->nativeformats & AST_FORMAT_TEXT_MASK )
	        r->AddTrack( TextCodec::T140, partName, textfile );
	
    }
    
    return (struct mp4rec *) r;
}

void Mp4RecorderDestroy( struct mp4rec * r )
{
    mp4recorder * r2 = (mp4recorder *) r;

    delete r2;    
}

int Mp4RecorderFrame( struct mp4rec * r, struct ast_frame * f )
{
   mp4recorder * r2 = (mp4recorder *) r;

   if (r2)
   {
	int rez = r2->ProcessFrame(f);

	if (rez == 0)
	{
	    struct ast_channel * chan = (struct ast_channel *) r2->GetCtxData();
	    if ( chan != NULL && (chan->nativeformats & AST_FORMAT_VIDEO_MASK) == 0 )
	    {
		/* AUdio frame ignored because we are waiting for video but no video on this chan
		 * do not wait for video anymore and ... process the frame.
		 */
		r2->SetWaitForVideo(false);
		Log("-mp4recorder: disable video waiting as chan %s does not support video (process frame).\n",
		    chan->name);
		rez = r2->ProcessFrame(f);
	    }
	}

	return rez;
   }
   else
	return -5;
}

int Mp4RecorderHasVideoStarted( struct mp4rec * r )
{
    mp4recorder * r2 = (mp4recorder *) r;

    return r2->IsVideoStarted();
}

void Mp4RecorderSetInitialDelay( struct mp4rec * r, unsigned long ms)
{
	mp4recorder * r2 = (mp4recorder *) r;
	
	r2->SetInitialDelay(ms);
}

struct mp4play * Mp4PlayerCreate(struct ast_channel * chan, MP4FileHandle mp4, bool transcodeVideo, int renderText)
{
	mp4player * p = new mp4player(chan, mp4);
	
	if (p)
	{
	    int haveAudio           =  chan->nativeformats & AST_FORMAT_AUDIO_MASK ;
	    int haveVideo           =  chan->nativeformats & AST_FORMAT_VIDEO_MASK ;  
	    int haveText            =  chan->nativeformats & AST_FORMAT_TEXT_MASK ;  
	    
	    
	    
	    TextCodec::Type tc = (TextCodec::Type) -1;
	    
	    if ( haveAudio )
	    {
			AudioCodec::Type acodecList[3];
			unsigned int nbACodecs = 0;
			AudioCodec::Type ac = (AudioCodec::Type) -1;

			if ( ! AstFormatToCodecList(chan->writeformat, ac) )
			{
				delete p;
				Error("mp4play: Failed to obtain preferred audio codec for chan %s\n", chan->name);
				return NULL;
			}
			
			nbACodecs = AstFormatToCodecList(chan->nativeformats, acodecList, 3);
			
			if ( p->OpenTrack(acodecList, nbACodecs, ac, true) < 0 )
			{
				Error("mp4play: [%s] No suitable audio track found.\n", chan->name);
			}
	    }
	    
	    if ( haveVideo )
	    {
			VideoCodec::Type vcodecList[3];
			unsigned int nbVCodecs = 0;
			VideoCodec::Type vc = (VideoCodec::Type)-1;

			if ( AstFormatToCodecList(chan->writeformat, vc) )
			{
				delete p;
				Error("mp4play: Failed to obtain preferred video codec for chan %s\n", chan->name);
				return NULL;
			}
			
			vc = vcodecList[0];
			nbVCodecs = AstFormatToCodecList(chan->nativeformats, vcodecList, 3);
			
			if ( p->OpenTrack(vcodecList, nbVCodecs, vc, transcodeVideo, false) < 0 )
			{
				Error("mp4play: [%s]  No suitable video track found.\n", chan->name);
			}			
	    }
	    
	    if ( haveText )
	    {
	        if ( chan->nativeformats & AST_FORMAT_RED )
		    tc = TextCodec::T140RED;
		else
		    tc = TextCodec::T140;
	    }
		
		if ( p->OpenTrack(tc, renderText) < 0 )
		{
			Error("mp4play: [%s]  No suitable video track found.\n", chan->name);
		}			
    }
    return (mp4play *) p;
}




int Mp4PlayerPlayNextFrame(struct ast_channel * chan, struct mp4play * p)
{
	mp4player * p2 = (mp4player *) p;
	unsigned long wait = 0;
	int ret;
	
	MediaFrame * f = p2->GetNextFrame(ret, wait);
	
	if ( ret >= 0 )
	{		
		if ( f->HasRtpPacketizationInfo() )
		{
			MediaFrame::RtpPacketizationInfo & pinfo = f->GetRtpPacketizationInfo();
			struct ast_frame f2;
			if ( ! MediaFrameToAstFrame(f, f2) )
			{
				return -5; /* incompatible codec read from MP4 file or unsupported media */
			}
			
			for( MediaFrame::RtpPacketizationInfo::iterator it = pinfo.begin() ;
				 it != pinfo.end() ;
				 it++ )
			
			{
				MediaFrame::RtpPacketization * rtp = *it;
				f2.data = f->GetData() + rtp->GetPos();
				f2.datalen = rtp->GetSize();
				if ( rtp->IsMark() ) f2.subclass |= 1;
				
				if ( ast_write(chan, &f2) < 0)
				{
					return -6; /* write error */ 
				}
			}
			return (int) wait;
		}
		else
		{
			Debug("mp4play: Failed to build packetization info for frame.\n");
			return 0;
		}
	}
	else
	{
		/* report error */
		return ret;
	}
}

void Mp4PlayerDestroy( struct mp4play * p )
{
	mp4player * p2 = (mp4player *) p;
	if (p2) delete p2;
}
