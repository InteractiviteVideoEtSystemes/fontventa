extern "C"
{
    #include <asterisk/frame.h>
    #include <asterisk/channel.h>
}
#include "medkit/astcpp.h"
#include "astmedkit/mp4format.h"
#include "mp4track.h"
#include "medkit/picturestreamer.h"
#include "astmedkit/frameutils.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"
#include "h264/h264depacketizer.h"

mp4recorder::mp4recorder(void * ctxdata, MP4FileHandle mp4, bool waitVideo)
{
    this->ctxdata = ctxdata;
    this->mp4 = mp4;
    textSeqNo = 0xFFFF;
    videoSeqNo = 0xFFFF;
    vtc = NULL;
    this->waitVideo = waitVideo ? 1 : 0;
    Log("mp4recorder: created with waitVideo %s.\n", waitVideo ? "enabled" : "disabled" );
    audioencoder = NULL;
    depak = NULL;
    SetParticipantName( "participant" );
    gettimeofday(&firstframets,NULL);
	gettimeofday(&lastfur,NULL);
    initialDelay = 0;
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
		mediatracks[i] = NULL;
    }
	pcstream = NULL;
    waitNextVideoFrame = false;
	saveTxtInComment = true;
	addVideoPrologue = true;
}

const char * idxToMedia(int i)
{
    switch(i)
    {
	case 0:
	    return "Audio";

	case 1:
	    return "Video";

	case 2:
	    return "VideoDoc";

	case 3:
	    return "Text";

	default:
	    return "Unknown";
    }
}

mp4recorder::~mp4recorder()
{
	const MP4Tags * tags = MP4TagsAlloc();  

	MP4TagsSetEncodingTool(tags, "MP4Save asterisk application");
	MP4TagsSetArtist(tags, partName );

	if (mediatracks[MP4_TEXT_TRACK] != NULL)
	{
		Mp4TextTrack * txttrack = (Mp4TextTrack *) mediatracks[MP4_TEXT_TRACK];
		std::string texte;
		txttrack->GetSavedTextForVm(texte);
		
		if (saveTxtInComment && texte.length() > 0)
		{

			
			if ( ! MP4TagsSetComments(tags, texte.c_str())  )
			{
				ast_log(LOG_WARNING, "mp4recorder: Save text inside mp4 comment tag failed.\n");
			}

		}
	}
	
	MP4TagsStore(tags, mp4);
	MP4TagsFree( tags );
	
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
        if ( mediatracks[i] ) delete mediatracks[i];
    }

    if (audioencoder) delete audioencoder;
    if (depak) delete depak;
    if (pcstream) delete pcstream;
}

void mp4recorder::DumpInfo()
{
    const char * media;
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
        if ( mediatracks[i] )
	{
	    Log("%s track ID %d has %d samples.\n",
		 idxToMedia(i), mediatracks[i]->GetTrackId(),
		 mediatracks[i]->GetSampleId());
	}
    }
    Log("-----------------\n");
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
    Mp4VideoTrack * vt = (Mp4VideoTrack *) mediatracks[MP4_VIDEO_TRACK];
    if ( vt != NULL )
    {
		if (waitVideo == 0) return 1;
		if ( vt->IsVideoStarted() ) return 1;
		if (depak != NULL && depak->MayBeIntra() ) return 1;
		return 0;
    }
    return -1;
}

int mp4recorder::ProcessFrame( const MediaFrame * f, bool secondary )
{
    int trackidx;

    switch ( f->GetType() )
    {
        case MediaFrame::Audio:
		if ( mediatracks[MP4_AUDIO_TRACK] == NULL )
		{
			/* auto create audio track if needed */
			AudioFrame * f2 = (AudioFrame *) f;
			AddTrack( f2->GetCodec(), f2->GetRate(), partName );
		}
			
		if ( mediatracks[MP4_AUDIO_TRACK] )
		{
			if (waitVideo) return 0;
	    
			if ( mediatracks[MP4_AUDIO_TRACK]->IsEmpty() )
			{
				
				// adjust initial delay
				//if ( mediatracks[MP4_VIDEO_TRACK] )
				//{
					// Synchronize with video
					//mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( videoDelay );
				//}
				//else
				//{
				// no video
				if (addVideoPrologue)
				{
					DWORD delay = initialDelay + (getDifTime(&firstframets)/1000);
					
					Log("Adding %u of initial delay + video start for audio.\n", delay);
					mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay(delay);
				}
				else if (initialDelay > 0)
				{
					Log("Adding %u of initial delay for audio.\n", initialDelay);
					mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay(initialDelay);
				}					
				//}
			}

			int ret = mediatracks[MP4_AUDIO_TRACK]->ProcessFrame(f);
			//Log("Audio: track duration %u, real duration %u.\n", mediatracks[MP4_AUDIO_TRACK]->GetRecordedDuration(), 
			//    getDifTime(&firstframets)/1000);
			return ret;
		}
		else return -3;
	    
		break;

	case MediaFrame::Video:
	    trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
	    if ( mediatracks[trackidx] )
	    {
			VideoFrame * f2 = (VideoFrame *) f;
			Mp4VideoTrack * tr = (Mp4VideoTrack *)  mediatracks[trackidx];
		
			if ( tr->IsEmpty() )
			{
				Properties properties;

				if (pcstream == NULL)
				{
					DWORD delay = initialDelay + (getDifTime(&firstframets)/1000);
					Log("-mp4recorder: Initializing video prologue.\n" );
					Log("Adding %u of initial delay for video.\n", delay);
					pcstream = new  PictureStreamer();
					pcstream->SetCodec(tr->GetCodec(), properties);
					pcstream->SetFrameRate(25, 100, 50);
					pcstream->PaintBlackRectangle(640, 480);
					tr->SetInitialDelay(delay);
				}
			}
			
		    if (waitVideo > 0 && f2->IsIntra()) 
			{
				waitVideo--;
				if (waitVideo == 0)
				{
					videoDelay = initialDelay + (getDifTime(&firstframets)/1000);
					Log("-mp4recorder: video has started after %lu ms.\n", getDifTime(&firstframets)/1000 );
				}
				else 
				{
					Log("-mp4recorder: skipping first I-frame on purpose.\n");
					// this return code shoudl cause client to send FIR
					return -333;
				}
					
			}
			
			if (waitVideo > 0)
			{
				if (addVideoPrologue)
				{
					// We are still waiting for video				
					// Replace P-Frames with black frames
					VideoFrame * f3 = pcstream->Stream(false);				
					if (f3 != NULL)
					{
						// depaketize f3
						DWORD ts  = f2->GetTimeStamp();
						MediaFrame * f4;
						
						// Specific H.264. We would need to do it in the video frame class directly to remain multi codecs ...
						depak->ResetFrame();
						
						for( MediaFrame::RtpPacketizationInfo::iterator it = f3->GetRtpPacketizationInfo().begin() ;
							 it != f3->GetRtpPacketizationInfo().end() ;
							 it++ )
						
						{
							f4 = depak->AddPayload( f3->GetData() + (*it)->GetPos(), (*it)->GetSize(), (*it)->IsMark() );
						}
						
						if (f4) 
						{
							f4->SetTimestamp(ts);
							tr->ProcessFrame(f4);
						}
					}
				}
									
				if ( (getDifTime(&lastfur)/1000) > 2000)
				{
					gettimeofday(&lastfur, NULL);
					Debug("mp4recorder: still no I frame. Requesting it again.\n");
					return -333;
				}

				return 0;
			}
			
			if  ( f->GetTimeStamp() == 0) Log("mp4recorder: incorrect video timestamp = 0. Check asterisk version.\n");
			
			// TS drift - compensate - disabled for now
			DWORD realDuration = getDifTime(&firstframets)/1000;

			/* if ( realDuration > tr->GetRecordedDuration()
				 &&
				 realDuration - tr->GetRecordedDuration() > 1000 )
			{
				 videoDelay += 10;
			}
			*/

			//Log("Video: track duration %u, real duration %u.\n",tr->GetRecordedDuration(), 
			//    getDifTime(&firstframets)/1000);
			int ret = tr->ProcessFrame(f2);
			return ret;
		}
	    else
	    {
			return -3;
	    }
	    break;
	    
	case MediaFrame::Text:
		if ( mediatracks[MP4_TEXT_TRACK] == NULL )
		{
			/* auto create text track if needed */
			const char * n = &partName[0];
			AddTrack( TextCodec::T140, n , 0);
		}
		
	    if ( mediatracks[MP4_TEXT_TRACK] )
	    {
			if (waitVideo) return 0;

			if ( mediatracks[MP4_TEXT_TRACK]->IsEmpty() )
			{
				// adjust initial delay
				if ( mediatracks[MP4_VIDEO_TRACK] )
				{
					// Synchronize with video
					mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( videoDelay );
				}
				else
				{
					// no video
					mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( initialDelay + (getDifTime(&firstframets)/1000) );
				}
			}
			
			return mediatracks[MP4_TEXT_TRACK]->ProcessFrame(f);			
	    }
		return -3;
	
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
			int loss_detected = false;
			if ( AstFormatToCodec( f->subclass, vcodec ) )
			{
				if (videoSeqNo != 0xFFFF)
				{
					if (f->seqno != 0xFFFF)
					{
						if (f->seqno != videoSeqNo+1) loss_detected = true;
					}
				}
				
				if (loss_detected)
				{
					Log("video packet lost detected seqno=%d, expected =%d\n", f->seqno, videoSeqNo+1);
					waitNextVideoFrame = true;
					ret = -333; // Ask for a FUR
				}

				videoSeqNo = f->seqno;
				
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
							if ( strcasecmp(f->src, "RTP") == 0 )
							{
								//Log("H.264 - got mark. frame ts = %ld, timingsource=TS.\n", f->ts );
								vfh264->SetTimestamp( f->ts );
							}
							else
							{
								//Log("H.264 - got mark. frame ts = %ld, timingsource=internal.\n", f->ts );
								
								vfh264->SetTimestamp( getDifTime(&firstframets)/1000 );
							}
						   
							if (!waitNextVideoFrame)
							{
								ret = ProcessFrame( vfh264 );
							}
							else
							{
								Log("H.264 - ignoring incomplete frame  ts = %ld.\n", f->ts );
							}
							depak->ResetFrame();
							waitNextVideoFrame = false;
							return ret;
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
						// TODO: Accumulate all ast_frame in a single VideoFrame and pass it to processing
						// 
						VideoFrame vf(vcodec, f->datalen, false);
						if ( strcasecmp(f->src, "RTP") == 0 )
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
		break;
		
	    case AST_FRAME_TEXT:
	    {
		DWORD lost = 0, text_ts;
	        TextCodec::Type tcodec;
		TextFrame tf( true );

		//If not first
		if (textSeqNo != 0xFFFF && f->seqno != 0xFFFF)
			//Calculate losts
			lost = f->seqno - textSeqNo-1;

		//Update last sequence number
		textSeqNo = f->seqno;
		//Log("text frame seqno %d, lost %d\n", f->seqno, lost);

		// Generate timing INFO
		text_ts = getDifTime(&firstframets)/1000 ;

		if ( (f->subclass  & AST_FORMAT_TEXT_MASK) == AST_FORMAT_RED )
		{
		    // parse RED to recover lost packets
		    RTPRedundantPayload red( AST_FRAME_GET_BUFFER(f), f->datalen );
		    
		    if (lost > 0 && red.GetRedundantCount() > 0)
		    {
			Log("text frame seqno %d, lost %d\n", f->seqno, lost);
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
				Log("Recovering lost packet seqno %d from redundant data.", textSeqNo-i);
				ProcessFrame ( &tf );
			}
		    }

		    /* char ttr[800];
		    
		    strncpy( ttr, (const char *) red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
		    ttr[  red.GetPrimaryPayloadSize() ] = '\0';
		    Log("Primary data [%s] len %d.\n", ttr, red.GetPrimaryPayloadSize() ); */
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
	next[MP4_AUDIO_TRACK] = MP4_INVALID_TIMESTAMP;
	mediatracks[MP4_VIDEO_TRACK] = NULL;
	next[MP4_VIDEO_TRACK] = MP4_INVALID_TIMESTAMP;
	mediatracks[MP4_VIDEODOC_TRACK] = NULL;
	next[MP4_VIDEODOC_TRACK] = MP4_INVALID_TIMESTAMP;
	mediatracks[MP4_TEXT_TRACK]  = NULL;
	next[MP4_TEXT_TRACK] = MP4_INVALID_TIMESTAMP;
	redenc = NULL;	
	gettimeofday(&startPlaying,0);
	nextBOMorRepeat = MP4_INVALID_TIMESTAMP;
}


int mp4player::OpenTrack(AudioCodec::Type outputCodecs[], unsigned int nbCodecs, AudioCodec::Type prefCodec, bool cantranscode )
{
    if (nbCodecs > 0)
    {
		MP4TrackId hintId = -1 ;
		MP4TrackId trackId = -1;
		MP4TrackId lastHintMatch = MP4_INVALID_TRACK_ID;
		MP4TrackId lastTrackMatch = MP4_INVALID_TRACK_ID;
		AudioCodec::Type lastCodecMatch;
		AudioCodec::Type c;
		
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
			//Debug("found hint track %d (%s)\n", hintId,nm?nm: "null");
			
			/* Get associated track */
			trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
			
			/* Check it's good */
			if (trackId != MP4_INVALID_TRACK_ID)
			{
				/* Get type */
				const char * tt = MP4GetTrackType(mp4, trackId);

				if (tt != NULL && strcmp(tt, MP4_AUDIO_TRACK_TYPE) == 0)
				{
					char *name;
					
					MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);
					
					if (name == NULL)
					{
						c = AudioCodec::AMR;
						name = (char *) "AMR";
					}
					else 
					{
						if ( ! AudioCodec::GetCodecFor(name, c) )
						{
							Log("Unsupported audio codec %s for hint track ID %d.\n", name, hintId);
							MP4Free(name);
							name = NULL;
							goto audio_track_loop1;
						}
					}
					
					Debug("found hinted track %d (%s)\n", trackId,name?name: "null");
					if ( c == prefCodec )
					{
						// This is the preffered codec !
						// use it and stop here
						
						lastTrackMatch 	= trackId;
						lastHintMatch 	= hintId;
						lastCodecMatch 	= c;
						if (c != AudioCodec::AMR) MP4Free(name);
						name = NULL;
						break;
					}    
					else if ( lastTrackMatch == MP4_INVALID_TRACK_ID)
					{
						for (int i=0; i<nbCodecs; i++)
						{
							if ( outputCodecs[i] == c )
							{
								lastTrackMatch 	= trackId;
								lastHintMatch 	= hintId;
								lastCodecMatch 	= c;
							}
						}
					}

					if ( lastTrackMatch == MP4_INVALID_TRACK_ID )
					{
						Log("Codec %s is not compatible with requested output codecs.\n", name);
					}
					if (c != AudioCodec::AMR) MP4Free(name);
					name = NULL;
				}		
			}
			else
			{
				Log("No media track associated with hint track ID %d.\n", hintId);
			}
			
audio_track_loop1:
			idxTrack++;
			hintId = MP4FindTrackId(mp4, idxTrack , MP4_HINT_TRACK_TYPE, 0);
		}

		if ( lastTrackMatch == MP4_INVALID_TRACK_ID)
		{
			Log("Try reopening audio track without hint.\n");
			idxTrack = 0;
			trackId = MP4FindTrackId(mp4, idxTrack, MP4_AUDIO_TRACK_TYPE, 0);
			while (trackId != MP4_INVALID_TRACK_ID)
			{
				const char* nm = MP4GetTrackMediaDataName(mp4,trackId);
				Debug("found media track %d (%s)\n", trackId,nm?nm: "null");
			
				/* Get type */
				const char * tt = MP4GetTrackType(mp4, trackId);

				if (tt != NULL && strcmp(tt, MP4_AUDIO_TRACK_TYPE) == 0)
				{
					const char *name = MP4GetTrackMediaDataName(mp4,trackId);
					
					if (name == NULL)
					{
						c = AudioCodec::AMR;
					}
					else 
					{
						if ( ! AudioCodec::GetCodecFor(name, c) )
						{
							Log("Unsupported audio codec %d for hint track ID %d.\n", name, hintId);
							goto audio_track_loop2;
						}
					}
					
					if ( c == prefCodec )
					{
						// This is the preffered codec !
						// use it and stop here
						
						lastTrackMatch 	= trackId;
						lastHintMatch 	= hintId;
						lastCodecMatch 	= c;
						break;
					}    
					
					if ( lastTrackMatch  == MP4_INVALID_TRACK_ID)
					{
						for (int i=0; i<nbCodecs; i++)
						{
							if ( outputCodecs[i] == c )
							{
								lastTrackMatch 	= trackId;
								lastHintMatch 	= hintId;
								lastCodecMatch 	= c;
							}
						}
					}

					if ( lastTrackMatch == MP4_INVALID_TRACK_ID)
					{
						Debug("Codec %s is not compatible with requested output codecs.\n", name);
					}
				}

audio_track_loop2:		
				idxTrack++;
				trackId = MP4FindTrackId(mp4, idxTrack , MP4_AUDIO_TRACK_TYPE, 0);
			}
		}
		
		if ( lastTrackMatch != MP4_INVALID_TRACK_ID)
		{
			mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack(mp4, lastTrackMatch, lastHintMatch, lastCodecMatch);
			next[MP4_AUDIO_TRACK] = mediatracks[MP4_AUDIO_TRACK]->GetNextFrameTime();
			Log("Opened audio track ID %d.\n", lastTrackMatch);
			return 1;
		}
		else
		{
			 return -1;
		}
    }
}

int mp4player::OpenTrack(VideoCodec::Type outputCodecs[], unsigned int nbCodecs, VideoCodec::Type prefCodec, bool cantranscode, bool secondary )
{
    if (nbCodecs > 0)
    {
		MP4TrackId hintId = MP4_INVALID_TRACK_ID ;
		MP4TrackId trackId = MP4_INVALID_TRACK_ID;
		MP4TrackId lastHintMatch = MP4_INVALID_TRACK_ID;
		MP4TrackId lastTrackMatch = MP4_INVALID_TRACK_ID;
		int idxTrack = 0;
		VideoCodec::Type c;
		
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
				const char * tt = MP4GetTrackType(mp4, trackId);

				if (tt != NULL && strcmp(tt, MP4_VIDEO_TRACK_TYPE) == 0)
				{
					char *name;
					
					MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);
					
					if (name == NULL)
					{
						Log("No video codec %d for hint track ID %d.\n", name, hintId);
						goto video_track_loop;
					}
					else 
					{
						if ( ! VideoCodec::GetCodecFor(name, c) )
						{
							Log("Unsupported video codec %d for hint track ID %d.\n", name, hintId);
							MP4Free(name);
							name = NULL;
							goto video_track_loop;
						}
					}
					Debug("found hinted video track %d (%s)\n", trackId,name?name: "null");
					
					if ( c == prefCodec )
					{
						// This is the preffered codec !
						// use it and stop here
						Debug("Video track %d matches preferred codec %s\n", trackId, VideoCodec::GetNameFor(c));
						lastTrackMatch = trackId;
						lastHintMatch = hintId;
						MP4Free(name);
						name = NULL;
						break;
					}    
					
					if ( lastTrackMatch ==  MP4_INVALID_TRACK_ID)
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

					if ( lastTrackMatch == MP4_INVALID_TRACK_ID)
					{
						Debug("Video codec %s is not compatible with requested output codecs.\n", name);
					}
					MP4Free(name);
					name = NULL;
				}
			}
			else
			{
				Log("No media track associated with hint track ID %d.\n", hintId);
			}
			
video_track_loop:
			idxTrack++;
			hintId = MP4FindTrackId(mp4, idxTrack , MP4_HINT_TRACK_TYPE, 0);
		}

		if ( lastTrackMatch !=  MP4_INVALID_TRACK_ID)
		{
			if (secondary)
			{
				mediatracks[MP4_VIDEODOC_TRACK] = new Mp4VideoTrack(mp4, lastTrackMatch, lastHintMatch, c);
				next[MP4_VIDEODOC_TRACK] = mediatracks[MP4_VIDEODOC_TRACK]->GetNextFrameTime();
			}
			else
			{
				mediatracks[MP4_VIDEO_TRACK] = new Mp4VideoTrack(mp4, lastTrackMatch, lastHintMatch, c);
				next[MP4_VIDEO_TRACK] = mediatracks[MP4_VIDEO_TRACK]->GetNextFrameTime();
			}
			Log("Opened video track ID %d hint track %d.\n", lastTrackMatch, lastHintMatch);
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
        Error("Text track is already open.\n");
        return 0;
    }

    if ( c == TextCodec::T140RED)
    {
		redenc = new RTPRedundantEncoder(pt);
    }
    
    MP4TrackId textId = MP4FindTrackId(mp4, 0, MP4_SUBTITLE_TRACK_TYPE, 0);
    
    if (textId != MP4_INVALID_TRACK_ID)
    {
		mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack(mp4, textId);
		next[MP4_TEXT_TRACK] = mediatracks[MP4_TEXT_TRACK]->GetNextFrameTime();
		Log("Opened text track ID %d.\n", textId);
		if ( next[MP4_TEXT_TRACK] == MP4_INVALID_TIMESTAMP)
		{
			 Error("No valid subtitle sample !\n");
		}
		return 1;
    }
    else
    {
		Debug("No text track in this file.\n");
		mediatracks[MP4_TEXT_TRACK] = NULL;
		next[MP4_TEXT_TRACK] = MP4_INVALID_TIMESTAMP;
		return -1;
    }
}

bool mp4player::Eof(void)
{
    if ( mediatracks[MP4_AUDIO_TRACK] && next[MP4_AUDIO_TRACK] != MP4_INVALID_TIMESTAMP )
		return false;

    if ( mediatracks[MP4_VIDEO_TRACK] && next[MP4_VIDEO_TRACK] != MP4_INVALID_TIMESTAMP )
		return false;

    if ( mediatracks[MP4_TEXT_TRACK] && next[MP4_TEXT_TRACK] != MP4_INVALID_TIMESTAMP )
		return false;

    return true;
}

bool mp4player::GetCodec(AudioCodec::Type & codec) const
{
	if ( mediatracks[MP4_AUDIO_TRACK] )
	{
		 Mp4AudioTrack * audiot = (Mp4AudioTrack *) mediatracks[MP4_AUDIO_TRACK];
		
		codec = audiot->GetCodec();
		return true;
	}
	return false;
}

int mp4player::Rewind()
{
	gettimeofday(&startPlaying,0);
	for (int i=0; i<4; i++)
	{
		if ( mediatracks[i] )
		{
			mediatracks[i]->Reset();
			next[i] = mediatracks[i]->GetNextFrameTime();
		}
		else
		{
			next[i] = MP4_INVALID_TIMESTAMP;
		}
	}
}

bool mp4player::GetNextTrackAndTs(int & trackId, QWORD & ts)
{
	ts = MP4_INVALID_TIMESTAMP;
	
	for (int i=0; i<4; i++)
	{
		if ( mediatracks[i] && next[i] < ts )
		{
			ts = next[i];
			trackId = i;
		}
	}
	
	return (ts != MP4_INVALID_TIMESTAMP);
}

MediaFrame * mp4player::GetNextFrame( int & errcode, unsigned long & waittime )
{    
    //timeval tv ;
    //timespec ts;
	MediaFrame * f2 = NULL;
	QWORD t = 0;
	int trackId;
	
	//DWORD now = getUpdDifTime(&startPlaying);
	DWORD now = getDifTime(&startPlaying)/1000;
	
        if ( ! Eof() )
	{
		
		if ( ! GetNextTrackAndTs(trackId, t) )
		{
			errcode = -2;
			return NULL;
		}
		
		// Handle RTT rentransmission and regular BOM sending in idle phase
		// TODO: 
		if (redenc && nextBOMorRepeat != MP4_INVALID_TIMESTAMP)
		{
			if (now >= nextBOMorRepeat && now < next[MP4_TEXT_TRACK])
			{
				if (redenc->IsIdle())
					redenc->EncodeBOM();
				else
					redenc->Encode(NULL);
								
				if (redenc->IsIdle())
					nextBOMorRepeat = now + 5000;
				else
					nextBOMorRepeat = now + 100;
				
				return redenc->GetRedundantPayload();
			}
		}
		
		if ( now < t )
		{
			// we need to wait
			waittime = t - now;
			errcode = 0;
			//Debug("mp4play: case  now < t. waittime=%lu\n", waittime);
			return NULL;
		}
		
		if (mediatracks[trackId] == NULL)
		{
			next[trackId] = MP4_INVALID_TIMESTAMP;
			errcode = -3;
			f2 = NULL;
		}
		else
		{			
			f2 = (MediaFrame *) mediatracks[trackId]->ReadFrame();
			if ( f2 == NULL )
			{
				errcode = -4;
				return NULL;
			}
			
			//Debug("mp4play: got frame from media %d\n", trackId);
			next[trackId] = mediatracks[trackId]->GetNextFrameTime();
			
			if ( trackId == MP4_TEXT_TRACK )
			{
				// Special case for text
				if (redenc) 
				{
					DWORD ts = f2->GetTimeStamp();
					redenc->Encode(f2);
					f2 = redenc->GetRedundantPayload();
					f2->SetTimestamp(ts);
					nextBOMorRepeat = now + 100;
				}
				
				errcode = 1;
			}
			else if ( ! f2->HasRtpPacketizationInfo() )
			{
				if (! f2->Packetize(1400) )
				{
					errcode = -5;
				}
				else
				{
					errcode = 1;
				}
			}
			else
			{
				errcode = 1;
			}
		}
		
		if (  GetNextTrackAndTs(trackId, t) )
		{		
			if ( now >= t )
			{
				// we do not need to wait
				//Debug("no need to wait after frame is received f2=%p, now=%lld, ts =%lld\n", f2, now, t);
				waittime = 0;
			}
			else
			{
				// we need to wait
				waittime = t - now;
				
				// If RTT is used
				if (redenc)
				{
					if (redenc->IsIdle())
					{
						if (waittime > 5000) waittime = 5000;
					}
					else
					{
						if (waittime > 100) waittime = 100;
					}
				}		
			}
		}
		else
		{
			//failed to get next TS, probably end of file
			waittime = 0;
		}
	}
	else
	{
		//mp4Play: eof
		errcode = -1;
	}
	return f2;
}

mp4player::~mp4player()
{
    for (int i =0; i < MP4_TEXT_TRACK + 1; i++)
    {
        if ( mediatracks[i] ) delete mediatracks[i];
    }

	if (redenc) delete redenc;	
}

void mp4recorder::Flush()
{
	if (mediatracks[MP4_VIDEO_TRACK])
	{
		Mp4VideoTrack * tr = (Mp4VideoTrack *)  mediatracks[MP4_VIDEO_TRACK];
		tr->WriteLastFrame();
	}
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
        waitVideo = 0;
	Log("-mp4recorder: disable video waiting as char %s does not support video.\n",
	    chan->name);
    }
    
    mp4recorder * r = new mp4recorder(chan, mp4, waitVideo);
    if ( partName == NULL ) partName = chan->cid.cid_name ? chan->cid.cid_name: "unknown";
    if ( r != NULL)
    {
#define MP4_SUPPORTED_AUDIO_FMT ( AST_FORMAT_ALAW | AST_FORMAT_AMRNB | AST_FORMAT_ULAW )

		int audio = (chan->nativeformats & AST_FORMAT_AUDIO_MASK);
		
		
		if ( audio !=0 && (audio & MP4_SUPPORTED_AUDIO_FMT) == 0)
		{
			Log("-mp4recorder: no supported audio codec. Defaulting to U-Law.\n");
			ast_set_read_format( chan, AST_FORMAT_ULAW );
		}
		
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
    r2->DumpInfo();
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

void Mp4RecorderFlush( struct mp4rec * r )
{
	mp4recorder * r2 = (mp4recorder *) r;
	
	r2->Flush();	
}

void Mp4RecorderEnableVideoPrologue( struct mp4rec * r, bool yesno )
{
	mp4recorder * r2 = (mp4recorder *) r;
	r2->EnableVideoPrologue(yesno);
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
			AudioCodec::Type acodecList[10];
			unsigned int nbACodecs = 0;
			AudioCodec::Type ac = (AudioCodec::Type) -1;
			int ast_codec = 0;

			if ( ! AstFormatToCodecList(chan->writeformat, &ac) )
			{
				delete p;
				Error("mp4play: Failed to obtain preferred audio codec for chan %s\n", chan->name);
				Error("mp4play: write format is %s (%x).\n", ast_getformatname(chan->writeformat), chan->writeformat);
				return NULL;
			}
			
			// Add additionnal codecs to activate trancoding if nativeformat are not enough
			haveAudio |= AST_FORMAT_ALAW | AST_FORMAT_ULAW | AST_FORMAT_SLINEAR ;
			
			nbACodecs = AstFormatToCodecList(haveAudio, acodecList, 10);
			
			if ( p->OpenTrack(acodecList, nbACodecs, ac, true) < 0 )
			{
				Error("mp4play: [%s] No suitable audio track found.\n", chan->name);
			}
			else
			{
				if ( p->GetCodec(ac) )
				{
					CodecToAstFormat(ac, ast_codec);
				
					Log("mp4play: [%s] activating audio transcoding from %s.\n", chan->name, AudioCodec::GetNameFor(ac) );
					if ( (chan->nativeformats & ast_codec) == 0 )
					{
						ast_set_write_format(chan, ast_codec); 
					}
				}
			}
	    }
	    
	    if ( haveVideo )
	    {
			VideoCodec::Type vcodecList[3];
			unsigned int nbVCodecs = 0;
			VideoCodec::Type vc = (VideoCodec::Type)-1;

			if ( ! AstFormatToCodecList(chan->nativeformats, &vc) )
			{
				delete p;
				Error("mp4play: Failed to obtain preferred video codec for chan %s\n", chan->name);
				return NULL;
			}
			
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
		if ( p->OpenTrack(tc, renderText, 1) < 0 )
		{
			Log("mp4play: [%s]  No suitable text track found.\n", chan->name);
		}			
	    }
    }
    return (mp4play *) p;
}




int Mp4PlayerPlayNextFrame(struct ast_channel * chan, struct mp4play * p)
{
	mp4player * p2 = (mp4player *) p;
	unsigned long wait = 0;
	int ret = -1;
	
	while (wait == 0)
	{
		MediaFrame * f = p2->GetNextFrame(ret, wait);
	
		if ( f == NULL )
		{
			if ( ret == 0 )
				return wait;
			else
			{
				if (ret != -1) Error("GetNextFrame returned %d.\n", ret);
				return ret;
			}
		}

		if ( f->HasRtpPacketizationInfo() )
		{
			MediaFrame::RtpPacketizationInfo & pinfo = f->GetRtpPacketizationInfo();
			struct ast_frame f2;
			
			for( MediaFrame::RtpPacketizationInfo::iterator it = pinfo.begin() ;
				 it != pinfo.end() ;
				 it++ )
			
			{
				bool  first = (it == pinfo.begin());
				
				if ( ! MediaFrameToAstFrame2(f, *it, first, f2, p2->buffer, sizeof(p2->buffer))  )
				{
					return -5; /* incompatible codec read from MP4 file or unsupported media */
				}
				
				if (f->GetType() == MediaFrame::Audio)
				{
					if (f2.subclass != chan->writeformat)
					{
						Log("mp4play: activating audio transcoding.\n");
						ast_set_write_format(chan, f2.subclass);
					}
				}
				
				if ( ast_write(chan, &f2) < 0)
				{
					Error("mp4play: failed to write frame with format %x.\n", f2.subclass );
					return -6; /* write error */ 
				}
				
				//no need to free. Everything is static
				//ast_frfree(&f2);
			}
			f->ClearRTPPacketizationInfo();
		}
		else
		{
			Debug("mp4play: Failed to get packetization info for frame.\n");
		}
		//no need to free f it is recycled by the media track.
	}
	
	
	return (int) wait;

}

void Mp4PlayerDestroy( struct mp4play * p )
{
	mp4player * p2 = (mp4player *) p;
	if (p2) delete p2;
}
