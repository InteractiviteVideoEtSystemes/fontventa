#include "mp4format.h"
#include "transcoder.h"
#include "medikit/red.h"

#if ASTERISK_VERSION_NUM>999999   // 10600
#define AST_FRAME_GET_BUFFER(fr)        ((uint8_t *)((fr)->data.ptr))
#else
#define AST_FRAME_GET_BUFFER(fr)        ((uint8_t *)((fr)->data))
#endif

static unsigned char silence_alaw[] = 
{
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
	
static unsigned char silence_ulaw[] = 
{
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


class Mp4AudioTrack : public mp4track
{
public:
    Mp4AudioTrack(MP4FileHandle mp4) : mp4track(mp4) { }
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );
    
private:
    AudioCodec codec;

};

class Mp4VideoTrack : public mp4track
{
public:
    Mp4VideoTrack(MP4FileHandle mp4) : mp4track(mp4)
    {
        width = 0;
	height =0;
	waitvideo = true;
	AVCProfileIndication 	= 0x42;	//Baseline
	AVCLevelIndication	= 0x0D;	//1.3
	AVCProfileCompat	= 0xC0;
    }
    
    void SetSize(DWORD width, DWORD height)
    {
	this->width = width;
	this->height = height;
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );
    bool IsWaitingVideo() { return waitVideo; }
    
    void SetH264ProfileLevel( unsigned char profile, unsigned char constraint, unsigned char level )
    {
	AVCProfileIndication = profile;
	AVCProfileCompat = constraints;
	AVCLevelIndication = level;
    }

private:
    DWORD width, height;
    bool firstpkt;
    bool intratrame;
    bool waitVideo;

    //H264 profile/level
    unsigned char AVCProfileIndication;
    unsigned char AVCLevelIndication;
    unsigned char AVCProfileCompat;
    
    VideoCodec codec;
};

#define MAX_SUBTITLE_DURATION 7000

class Mp4TextTrack : public mp4track
{
public:
    Mp4TextTrack(MP4FileHandle mp4) : mp4track(mp4) { }
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );
    
private:
    TextEncoder encoder;
    MP4TrackId rawtexttrack;
};

int Mp4AudioTrack::Create(const char * trackName, int codec, DWORD bitrate)
{
    // Create audio track
    int type;
    switch (codec)
    {
        case AudioCodec::PCMA:
	    mediatrack = MP4AddALawAudioTrack(mp4,rate);
	    // Set channel and sample properties
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.alaw.channels", 1);
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.alaw.sampleSize", 8);
	    // Create audio hint track
	    hinttrack = MP4AddHintTrack(mp4, mediatrack);
	    // Set payload type for hint track
	    type = 8;
	    MP4SetHintTrackRtpPayload(mp4, hinttrack, "PCMA", &type, 0, NULL, 1, 0);
	    break;
	
	case AudioCodec::PCMU:
	    mediatrack = MP4AddULawAudioTrack(mp4,rate);
	    // Create audio hint track
	    hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
	    type = 0;
	    MP4SetHintTrackRtpPayload(mp4, hinttrack, "PCMU", &type, 0, NULL, 1, 0);
	     // Set channel and sample properties
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.ulaw.channels", 1);
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.ulaw.sampleSize", 8);
	    break;

	case AudioCodec::AMR
	    mediatrack = MP4AddAmrAudioTrack(mp4, rate, 0, 0, 1, 0);;
	    // Create audio hint track
	    hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
	    type = 98;
	    MP4SetHintTrackRtpPayload(mp4, hinttrack, "AMR", &type, 0, NULL, 1, 0);
	    MP4SetAudioProfileLevel(mp4, 0xFE);
	    break;
	
	// TODO: Add AAC support
	
	default:
	    Log("-mp4recorder: unsupported codec %s for audio track.\n", AudioCodec::GetNameFor(codec));
	    return 0;
    }
    
    if ( IsOpen() && trackName != NULL ) MP4SetTrackName( mp4, mediatrack, trackName );
    this->codec = (AudioCodec::Type) codec;
    
    return 1;
}

int Mp4AudioTrack::ProcessFrame( MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Audio )
    {
        AudioFrame f2 = ( AudioFrame *) f;
		
	if ( f2->GetCodec() == codec )
	{
	    DWORD duration;
	    
	    if (sampleId == 0)
	    {
	        duration = 20*f->GetRate()/1000;
	    }
	    else
	    {
	        duration = (f->GetTimeStamp()-prevts)*f->GetRate()/1000;
	    }
	    prevts = f->GetTimeStamp();
	    MP4WriteSample(mp4, mediatrack, f->GetData(), f->GetLength(), duration, 0, 1);
	    sampleId++;

	    if (hinttrack != MP4_INVALID_TRACK_ID)
	    {
		// Add rtp hint
		MP4AddRtpHint(mp4, hinttrack);

		///Create packet
		MP4AddRtpPacket(mp4, hinttrack, 0, 0);

		// Set full frame as data
		MP4AddRtpSampleData(mp4, hinttrack, sampleId, 0, f->GetLength());

		// Write rtp hint
		MP4WriteRtpHint(mp4, hinttrack, duration, 1);
	    
	    }
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return -2;
}

int Mp4VideoTrack::Create(const char * trackName, int codec, DWORD bitrate)
{
	BYTE type;

	//Check the codec
	switch (codec)
	{
		case VideoCodec::H263_1996:
			// Create video track
			mediatrack = MP4AddH263VideoTrack(mp4, 90000, 0, width, height, 0, 0, 0, 0);
			// Create video hint track
			hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
			type = 34;
			MP4SetHintTrackRtpPayload(mp4, hinttrack, "H263", &type, 0, NULL, 1, 0);
			break;

		case VideoCodec::H263_1998:
			// Create video track
			mediatrack = MP4AddH263VideoTrack(mp4, 90000, 0, width, height, 0, 0, 0, 0);
			// Create video hint track
			hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
			type = 96;
			MP4SetHintTrackRtpPayload(mp4, hinttrack, "H263-1998", &type, 0, NULL, 1, 0);
			break;

		case VideoCodec::H264:
			MP4Duration h264FrameDuration		= 1.0/30;
			// Create video track
			mediatrack = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, width, height, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
			// Create video hint track
			hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
			type = 99;
			MP4SetHintTrackRtpPayload(mp4, hinttrack, "H264", &type, 0, NULL, 1, 0);
			break;

		default:
		    Log("-mp4recorder: unsupported codec %s for video track.\n", VideoCodec::GetNameFor(codec));
		    return 0;
	}
	this->codec = codec;
	if ( IsOpen() && trackName != NULL ) MP4SetTrackName( mp4, mediatrack, trackName );
	Log("-mp4recorder: created video track %d using codec %s.\n", mediatrack, VideoCodec::GetNameFor(codec));

}


int Mp4VideoTrack::ProcessFrame( MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Video )
    {
        VideoFrame f2 = ( VideoFrame *) f;
	
	fi (f2->GetType() == codec )
	{
	    DWORD duration;
	    
	    if (sampleId == 0)
	    {
	        duration = 90/15;
	    }
	    else
	    {
	        duration = (f2->GetTimeStamp()-prevts)*90;
	    }
	    prevts = f->GetTimeStamp();
	    MP4WriteSample(mp4, mediatrack, f->GetData(), f->GetLength(), duration, 0, 1);
	    sampleId++;

    	    MP4WriteSample(mp4, mediatrack, f2->GetData(), f2->GetLength(), duration, 0, f2->IsIntra());

	    //Check if we have rtp data
	    if (f2->HasRtpPacketizationInfo())
	    {
		//Get list
		MediaFrame::RtpPacketizationInfo& rtpInfo = frame->GetRtpPacketizationInfo();
		//Add hint for frame
		MP4AddRtpHint(mp4, hint);
		//Get iterator
		MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin();
		//Latest?
		bool last = (it==rtpInfo.end());

		//Iterate
		while(!last)
		{
			//Get rtp packet and move to next
			MediaFrame::RtpPacketization *rtp = *(it++);
			//is last?
			last = (it==rtpInfo.end());
			//Create rtp packet
			MP4AddRtpPacket(mp4, hinttrack, last, 0);

			//Check rtp payload header len
			if (rtp->GetPrefixLen())
				//Add rtp data
				MP4AddRtpImmediateData(mp4, hinttrack, rtp->GetPrefixData(), rtp->GetPrefixLen());

			//Add rtp data
			MP4AddRtpSampleData(mp4, hinttrack, sampleId, rtp->GetPos(), rtp->GetSize());

			//It is h264 and we still do not have SPS or PPS?
			if (f2->GetCodec()==VideoCodec::H264 && (!hasSPS || !hasPPS))
			{
				//Get rtp data pointer
				BYTE *data = f2->GetData()+rtp->GetPos();
				//Check nal type
				BYTE nalType = data[0] & 0x1F;
				//Get nal data
				BYTE *nalData = data+1;
				DWORD nalSize = rtp->GetSize()-1;

				//If it a SPS NAL
				if (!hasSPS && nalType==0x07)
				{
					H264SeqParameterSet sps;
					//DEcode SPS
					sps.Decode(nalData,nalSize);
					//Dump
					sps.Dump();
					//Update widht an ehight
					MP4SetTrackIntegerProperty(mp4,track,"mdia.minf.stbl.stsd.avc1.width", sps.GetWidth());
					MP4SetTrackIntegerProperty(mp4,track,"mdia.minf.stbl.stsd.avc1.height", sps.GetHeight());
					//Add it
					MP4AddH264SequenceParameterSet(mp4,mediatrack,nalData,nalSize);
					//No need to search more
					hasSPS = true;
				}

				//If it is a PPS NAL
				if (!hasPPS && nalType==0x08)
				{
					//Add it
					MP4AddH264PictureParameterSet(mp4,mediatrack,nalData,nalSize);
					//No need to search more
					hasPPS = true;
				}
			}
		}

		//Save rtp
		MP4WriteRtpHint(mp4, hinttrack, duration, frame->IsIntra());
	    }
	    return 1;
	}
	else
	{
	    return -1;
	}

    }
    return 0;
}

int Mp4TextFrameTrack::Create(const char * trackName, int codec, DWORD bitrate)
{
    mediatrack = MP4AddSubtitleTrack(mp4,1000,384,60);
    if ( IsOpen() && trackName != NULL ) MP4SetTrackName( mp4, mediatrack, trackName );
}

int Mp4TextFrameTrack::ProcessFrame( MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Text )
    {
        TextFrame f2 = ( TextFrame *) f;
	DWORD duration = 0, frameduration = 0;
	DWORD subtsize = 0;

	if ( f2->GetLength() == 0 ) return 0;

	//Set the duration of the frame on the screen
	
	if (sampleId == 0)
	{
	        frameduration = 20;
	}
	else
	{
	        frameduration = (f2->GetTimeStamp()-prevts);
	}
	
	duration = frameduration;
	if (frameduration > MAX_SUBTITLE_DURATION) frameduration = MAX_SUBTITLE_DURATION;
	
	sampleId++;
	
	encoder.Accumulate( f2->GetWString() );
	TextFrame * f3 = GetSubtitle();

	if (f3)
	{
	    subsize = f3->GetLength();
	    BYTE* data = (BYTE*)malloc(subsize+2);

	    //Set size
	    data[0] = subsize>>8;
	    data[1] = subsize & 0xFF;
	    
	    memcpy(data+2,f3->GetData(),f3->GetLength());
	    
	    MP4WriteSample( mp4, mediatrack, data, subsize+2, frameduration, 0, false );
	    
	    if (duration > MAX_SUBTITLE_DURATION)
	   {
		frameduration = duration - MAX_SUBTITLE_DURATION;
		//Log
		//Put empty text
		data[0] = 0;
		data[1] = 0;

		//Write sample
		MP4WriteSample( mp4, track, data, 2, frameduration, 0, false );
	    }
	}
	return 1;
    }
    return 0;
}


mp4recorder::mp4recorder(void * ctxdata, MP4FileHandle mp4)
{
    this->ctxdata = ctxdata;
    this->mp4 = mp4;
    textSeqNo = 0;
    vtc = NULL;
    audioencoder = NULL;
}

int mp4recorder::AddTrack(AudioCodec codec, DWORD samplerate, const char * trackName)
{
    if ( mediatracks[MP4_AUDIO_TRACK] == NULL )
    {
	mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack(mp4);
	if ( mediatracks[MP4_AUDIO_TRACK] )
	{
	    mediatracks[MP4_AUDIO_TRACK]->Create( trackName, codec, sampleRate );
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}

int mp4recorder::AddTrack(VideoCodec codec, DWORD width, DWORD height, DWORD bitrate, const char * trackName, bool secondary )
{
    int trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
    
    if ( mediatracks[trackidx] == NULL )
    {
	mediatracks[trackidx] = new Mp4VideoTrack(mp4);
	if ( mediatracks[trackidx] )
	{
	    mediatracks[trackidx]->SetSize(width, height);
	    mediatracks[trackidx]->Create( trackName, codec, bitrate );
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}

int mp4recorder::AddTrack(TextCodec codec, const char * trackName)
{
    if ( mediatracks[MP4_TEXT_TRACK] == NULL )
    {
	mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack(mp4);
	if ( mediatracks[MP4_TEXT_TRACK] )
	{
	    mediatracks[MP4_TEXT_TRACK]->Create( trackName, codec, 1000 );
	    return 1;
	}
	else
	{
	    return -1;
	}
    }
    return 0;
}


int mp4recorder::ProcessFrame( const MediaFrame * f, bool secondary )
{
    switch ( f->GetType() )
    {
        case MediaFrame::Audio:
	    if ( mediatracks[MP4_AUDIO_TRACK] )
	    {
	        return mediatracks[MP4_AUDIO_TRACK]->ProcessFrame(f);
	    }
	    else
	    {
		return -3;
	    }
	    break;
	    
	case MediaFrame::Video:
	    trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
	    if ( mediatracks[trackidx] )
	    {
	        return mediatracks[trackidx]->ProcessFrame(f);
	    }
	    else
	    {
		return -3;
	    }
	    break;
	    
	case MediaFrame::Text:
	    if ( mediatracks[MP4_TEXT_TRACK] )
	    {
	        return mediatracks[MP4_TEXT_TRACK]->ProcessFrame(f);
	    }
	    else
	    {
		return -3;
	    }
	    break;
	
	default:
	    break;
    }
}

bool AstFormatToCodec( int format, AudioCodec::Type & codec )
{
    switch ( format )
    {
        case AST_FORMAT_ULAW:
	    codec = AudioCodec::PCMU;
	    break;
	    
	case AST_FORMAT_ALAW:
	    codec = AudioCodec::PCMA;
	    break;
	    
	case AST_FORMAT_AMR:
	    codec = AudioCodec::AMR;
	    break;
	
	default:
	    return false;
    }
    return true;
}
	
bool AstFormatToCodec( int format, VideoCodec::Type & codec )
{
    switch ( format )
    {
        case AST_FORMAT_H263:
	    codec = VideoCodec::H263_1996;
	    break;
	    
	case AST_FORMAT_H263P:
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
		    int outLen = 800;
		    acodec = AudioCodec::PCMU;
		    
		    if (audioencoder == NULL)
			audioencoder = CreateEncoder(AudioCodec::PCMU);
   
		    outLen = audioencoder->Encode( AST_FRAME_GET_BUFFER(f), f->datalen, audioBuff, outLen );
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
		return ProcessFrame( af );
	    }
	    		
	    case AST_FRAME_VIDEO:
	    {
	        VideoCodec vcodec;
		
		if ( AstFormatToCodec( f->subclass, vcodec ) )
		{
		    VideoFrame vf(vcodec, f->datalen, false);
		    
		    vf.SetTimeStamp(f->ts);
		    
		    // Handle packetization here
		    vf.SetMedia( AST_FRAME_GET_BUFFER(f), f->datalen );
		    int ret = ProcessFrame( vf );
		    
		    if ( ret == -1 && vtx != NULL)
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
		DWORD lost = 0;
	        TextCodec::Type tcodec;
		TextFrame tf( TextCodec::T140, 1000, false );

		//If not first
		if (textSeqNo != 0xFFFF)
			//Calculate losts
			lost = f->seqno - textSeqNo-1;

		//Update last sequence number
		lastSeq = f->seqno;
		
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
			for (int i=red.GetRedundantCount()-lost;i<red->GetRedundantCount();i++)
			{
				//Create frame from recovered data - check timestamps ...
				tf.SetTimeStamp( red.GetRedundantTimestamp(i) );
				tf.SetMedia( red.GetRedundantPayloadData(i),red.GetRedundantPayloadSize(i) );
				ProcessFrame ( tf );
			}
		    }
		    
		    tf.SetTimeStamp( f->ts );
		    tf.SetMedia( red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
		}
		else /* assume plain text */
		{
		    tf.SetTimeStamp( f->ts );
		    tf.SetMedia( AST_FRAME_GET_BUFFER(f), f->datalen );
		}
		
		return ProcessFrame( af );
	    }	    
}

/* ---- callbeck used for video transcoding --- */

void Mp4RecoderVideoCb(void * ctxdata, int outputcodec, const char *output, size_t outputlen)
{
    mp4recorder * r2 = (mp4recorder *) ctxdata;
    VideoFrame vf(outputcodec, 2000, false);
    
    if (r2)
    {
        vf.SetMedia(output, outputlen);
	// add timestamp
	r2->ProcessFrame(vf);
    }
}    

struct mp4rec * Mp4RecorderCreate(struct ast_channel * chan, MP4FileHandle mp4, char * videoformat)
{
    mp4recorder * r = new mp4recorder(chan, mp4);
    
    return (struct mp4rec *) r;
}

void Mp4RecorderDestroy( struct mp4rec * r );
{
    mp4recorder * r2 = (mp4recorder *) r;

    delete r2;    
}

int Mp4RecorderFrame( struct mp4rec * r, struct ast_frame * f )
{
   mp4recorder * r2 = (mp4recorder *) r;

   if (r2)
	return r2->ProcessFrame(f);
   else
	return -5;
}

