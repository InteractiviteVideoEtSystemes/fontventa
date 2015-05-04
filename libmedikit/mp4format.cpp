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
#include "h264/h264.h"
#include "h264/h264depacketizer.h"

class Mp4AudioTrack : public Mp4Basetrack
{
public:
    Mp4AudioTrack(MP4FileHandle mp4, unsigned long delay) : Mp4Basetrack(mp4, delay) { }
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    
private:
    AudioCodec::Type codec;

};

class Mp4VideoTrack : public Mp4Basetrack
{
public:
    Mp4VideoTrack(MP4FileHandle mp4, unsigned long delay) : Mp4Basetrack(mp4, delay)
    {
        width = 0;
		height =0;
		videoStarted = false;
		AVCProfileIndication 	= 0x42;	//Baseline
		AVCLevelIndication	= 0x0D;	//1.3
		AVCProfileCompat	= 0xC0;
		hasSPS = false;
		hasPPS = false;
    }
    
    void SetSize(DWORD width, DWORD height)
    {
	this->width = width;
	this->height = height;
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    bool IsVideoStarted() { return videoStarted; }
    
    void SetH264ProfileLevel( unsigned char profile, unsigned char constraint, unsigned char level )
    {
	AVCProfileIndication = profile;
	AVCProfileCompat = constraint;
	AVCLevelIndication = level;
    }

private:
    DWORD width, height, bitrate;
    bool videoStarted;
    bool firstpkt;
    bool intratrame;
    bool hasSPS;
    bool hasPPS;
    
    //H264 profile/level
    unsigned char AVCProfileIndication;
    unsigned char AVCLevelIndication;
    unsigned char AVCProfileCompat;
    
    VideoCodec::Type codec;
    std::string trackName;
};

#define MAX_SUBTITLE_DURATION 7000

class Mp4TextTrack : public Mp4Basetrack
{
public:
    Mp4TextTrack(MP4FileHandle mp4, int textfile, unsigned long delay) : Mp4Basetrack(mp4, delay) 
    { 
	this->textfile = textfile;
    }
    
    virtual ~Mp4TextTrack();
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    
private:
    TextEncoder encoder;
    MP4TrackId rawtexttrack;
    int textfile;
};

int Mp4AudioTrack::Create(const char * trackName, int codec, DWORD samplerate)
{
    // Create audio track
    
    uint8_t type;
    switch (codec)
    {
        case AudioCodec::PCMA:
	    mediatrack = MP4AddALawAudioTrack(mp4, 8000);
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
	    mediatrack = MP4AddULawAudioTrack(mp4, 8000);
	    // Create audio hint track
	    hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
	    type = 0;
	    MP4SetHintTrackRtpPayload(mp4, hinttrack, "PCMU", &type, 0, NULL, 1, 0);
	     // Set channel and sample properties
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.ulaw.channels", 1);
	    MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.ulaw.sampleSize", 8);
	    break;

	case AudioCodec::AMR:
	    mediatrack = MP4AddAmrAudioTrack(mp4, samplerate, 0, 0, 1, 0);
	    // Create audio hint track
	    hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
	    type = 98;
	    MP4SetHintTrackRtpPayload(mp4, hinttrack, "AMR", &type, 0, NULL, 1, 0);
	    MP4SetAudioProfileLevel(mp4, 0xFE);
	    break;
	
	// TODO: Add AAC support
	
	default:
	    Error("-mp4recorder: unsupported codec %s for audio track.\n", AudioCodec::GetNameFor((AudioCodec::Type) codec));
	    return 0;
    }
    
    if ( mediatrack != MP4_INVALID_TRACK_ID )
    {
	Log("-mp4recorder: opened audio track [%s] id:%d and hinttrack id:%d codec %s.\n", 
	    (trackName != NULL) ? trackName : "unnamed",
	    mediatrack, hinttrack, AudioCodec::GetNameFor((AudioCodec::Type) codec));
    }
    if ( IsOpen() && trackName != NULL ) MP4SetTrackName( mp4, mediatrack, trackName );
    this->codec = (AudioCodec::Type) codec;
    
    return 1;
}

int Mp4AudioTrack::ProcessFrame( const MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Audio )
    {
        const AudioFrame * f2 = ( AudioFrame *) f;
	if ( f2->GetCodec() == codec )
	{
	    DWORD duration;
	    
	    if (sampleId == 0)
	    {
		if ( initialDelay > 0 )
		{
			const AudioFrame *silence = GetSilenceFrame( codec );
			if (silence == NULL) silence = f2;
			// Add frame of 80 ms
			Log("Adding %d ms of initial delay on audio track id:%d.\n", initialDelay, mediatrack);
			for ( DWORD d = 0; d < initialDelay; d += 80 )
			{
			    if ( initialDelay - d > 80 )
				duration = 80;
			    else
				duration = initialDelay - d;
			}
			duration = duration*f2->GetRate()/1000;
			MP4WriteSample(mp4, mediatrack, silence->GetData(), silence->GetLength(), duration, 0, 1 );
			sampleId++;
		}
		duration = 20*f2->GetRate()/1000;
	    }
	    else
	    {
	        duration = (f->GetTimeStamp()-prevts)*f2->GetRate()/1000;
		if ( duration > (200 *f2->GetRate()/1000) || prevts > f->GetTimeStamp() )
		{
		    // Inconsistend duration
		    duration = 20*f2->GetRate()/1000;
		}
	    }
	    prevts = f2->GetTimeStamp();
	    MP4WriteSample(mp4, mediatrack, f2->GetData(), f2->GetLength(), duration, 0, 1 );
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
	    
	    if ( sampleId > 1 ) return 1;
	    
	    //Initial duration - repeat the fisrt frame
	    if ( initialDelay > 0 ) ProcessFrame(f);
	    
	    return 1;
	}
	else
	{
	    Log("Cannot process audio frame with codec %s. Track codec is set to %s.\n",
		GetNameForCodec(MediaFrame::Audio, f2->GetCodec()), 
		GetNameForCodec(MediaFrame::Audio, codec ) );		
	    return -1;
	}
    }
    return -2;
}

int Mp4VideoTrack::Create(const char * trackName, int codec, DWORD bitrate)
{
	BYTE type;
	MP4Duration h264FrameDuration;
		
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
			h264FrameDuration	= (1.0/30);
			// Create video track
			mediatrack = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, width, height, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
			// Create video hint track
			hinttrack = MP4AddHintTrack(mp4, mediatrack);
			// Set payload type for hint track
			type = 99;
			MP4SetHintTrackRtpPayload(mp4, hinttrack, "H264", &type, 0, NULL, 1, 0);
			break;

		default:
		    Log("-mp4recorder: unsupported codec %s for video track. Track not created.\n", 
		        VideoCodec::GetNameFor((VideoCodec::Type) codec));
		    return 0;
		    break;
	}
	this->codec = (VideoCodec::Type) codec;
	
	if ( trackName ) this->trackName = trackName; 
	
	if ( IsOpen() )
	{	
		if ( trackName) 
			MP4SetTrackName( mp4, mediatrack, trackName );
		else if ( ! this->trackName.empty() )
			MP4SetTrackName( mp4, mediatrack, this->trackName.c_str() );
	}
	
	Log("-mp4recorder: created video track [%s] id:%d, hinttrack id:%d using codec %s.\n", 
	    this->trackName.c_str(), mediatrack, hinttrack, VideoCodec::GetNameFor( (VideoCodec::Type) codec));

}


int Mp4VideoTrack::ProcessFrame( const MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Video )
    {
        VideoFrame * f2 = ( VideoFrame *) f;
	
	if (f2->GetCodec() == (VideoCodec::Type) codec )
	{	
	    DWORD duration;

	    // If video is not started, wait for the first I Frame
	    if ( ! videoStarted )
	    {
		if ( f2->IsIntra() )
		{
		    videoStarted = true;
		    Log("-mp4recorder: got the first I frame. Video recording starts on track id:%d.\n", mediatrack);
		}
		else
		{
		    return 0;
	        }
	    }
	    
	    
	    if (sampleId == 0)
	    {
		if ( initialDelay > 0 )
		{
			duration = initialDelay*90;
			Log("Adding %d ms of initial delay on video track id:%d.\n", initialDelay, mediatrack);
		}
		else
		{
			// 20 fps = (1000 / 20) * 90
			duration = 50*90;
		}
	    }
	    else if (sampleId == 1 && initialDelay > 0)
	    {
		duration = 50*90;
	    }
	    else
	    {
	        duration = (f2->GetTimeStamp()-prevts)*90;
	    }
	    prevts = f->GetTimeStamp();
	    sampleId++;

	    //Log("Process VIDEO frame  ts:%lu, duration %u.\n",  f2->GetTimeStamp(), duration);

    	    MP4WriteSample(mp4, mediatrack, f2->GetData(), f2->GetLength(), duration, 0, f2->IsIntra());

	    //Check if we have rtp data
	    if (f2->HasRtpPacketizationInfo())
	    {
		//Get list
		MediaFrame::RtpPacketizationInfo& rtpInfo = f2->GetRtpPacketizationInfo();
		//Add hint for frame
		MP4AddRtpHint(mp4, hinttrack);
		//Get iterator
		MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin();
		
		for (it = rtpInfo.begin(); it != rtpInfo.end(); it++)
		{
		    MediaFrame::RtpPacketization * rtp = *it;

		    if ( f2->GetCodec()==VideoCodec::H264 && (!hasSPS || !hasPPS) )
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
				// Update size
				width = sps.GetWidth();
				height = sps.GetHeight();
				
				//Add it
				MP4AddH264SequenceParameterSet(mp4,mediatrack,nalData,nalSize);
				//No need to search more
				hasSPS = true;
				
				// Update profile level
				AVCProfileIndication 	= sps.GetProfile();
				AVCLevelIndication	= sps.GetLevel();
				
				Log("-mp4recorder: new size: %lux%lu. H264_profile: %02x H264_level: %02x\n", 
				    width, height, AVCProfileIndication, AVCLevelIndication);
				
				//Update widht an ehight
				MP4SetTrackIntegerProperty(mp4,mediatrack,"mdia.minf.stbl.stsd.avc1.width", sps.GetWidth());
				MP4SetTrackIntegerProperty(mp4,mediatrack,"mdia.minf.stbl.stsd.avc1.height", sps.GetHeight());
				
				//Add it
				MP4AddH264SequenceParameterSet(mp4,mediatrack,nalData,nalSize);
				
				videoStarted = true;
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
		    
		    // It was before AddH264Seq ....
		    MP4AddRtpPacket(mp4, hinttrack, rtp->IsMark(), 0);

		    //Check rtp payload header len
		    if (rtp->GetPrefixLen())
			//Add rtp data
			MP4AddRtpImmediateData(mp4, hinttrack, rtp->GetPrefixData(), rtp->GetPrefixLen());

			//Add rtp data
		    MP4AddRtpSampleData(mp4, hinttrack, sampleId, rtp->GetPos(), rtp->GetSize());

		}
		
		//Save rtp
		MP4WriteRtpHint(mp4, hinttrack, duration, f2->IsIntra());	

	    }
	    
	    if ( sampleId > 1 ) return 1;
	    
	    //Initial duration - repeat the fisrt frame
	    if ( initialDelay > 0 ) ProcessFrame(f);
	}
	else
	{
	    return -1;
	}

    }
    return 0;
}

int Mp4TextTrack::Create(const char * trackName, int codec, DWORD bitrate)
{
    mediatrack = MP4AddSubtitleTrack(mp4,1000,384,60);
    if ( IsOpen() && trackName != NULL ) MP4SetTrackName( mp4, mediatrack, trackName );
    if ( IsOpen() ) Log("-mp4recorder: created text track %d.\n", mediatrack);
}

int Mp4TextTrack::ProcessFrame( const MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Text )
    {
        TextFrame * f2 = ( TextFrame *) f;
	DWORD duration = 0, frameduration = 0;
	DWORD subtsize = 0;
	std::string subtitle;

	if ( f2->GetLength() == 0 ) return 0;

	//Set the duration of the frame on the screen
	
	if (sampleId == 0)
	{
		if ( initialDelay > 0 )
		{
			BYTE silence[4];
			duration = initialDelay;
			//Set size
			silence[0] = 0;
			silence[1] = 0;
			MP4WriteSample( mp4, mediatrack, silence, 2, duration, 0, true );
			Log("Adding %d ms of initial delay on text track id:%d.\n", initialDelay, mediatrack);
		}
		else
		{
			duration = 1;
		}
	}
	else
	{
	        frameduration = (f2->GetTimeStamp()-prevts);
	}

	//Log("Process TEXT frame  ts:%lu, duration %u [%ls].\n",  f2->GetTimeStamp(), frameduration, f2->GetWString().c_str());
	prevts = f->GetTimeStamp();	
	duration = frameduration;
	if (frameduration > MAX_SUBTITLE_DURATION) frameduration = MAX_SUBTITLE_DURATION;
	
	sampleId++;
	
	
	if ( encoder.Accumulate( f2->GetWString() ) == 2)
	{
	    // Current line has just been flushed into history
	    if ( textfile >= 0 ) 
	    {
	        // If there is an active text file, write it
		encoder.GetFirstHistoryLine(subtitle);
		write( textfile, subtitle.c_str(), subtitle.length() );
	    }
	}
	encoder.GetSubtitle(subtitle);
	unsigned int subsize = subtitle.length();
	
	BYTE* data = (BYTE*)malloc(subsize+2);

	//Set size
	data[0] = subsize>>8;
	data[1] = subsize & 0xFF;
	
	memcpy(data+2,subtitle.c_str(), subsize);
	    
	MP4WriteSample( mp4, mediatrack, data, subsize+2, frameduration, 0, true );
	    
	if (duration > MAX_SUBTITLE_DURATION)
	{
		frameduration = duration - MAX_SUBTITLE_DURATION;
		//Log
		//Put empty text
		data[0] = 0;
		data[1] = 0;

		//Write sample
		MP4WriteSample( mp4, mediatrack, data, 2, frameduration, 0, false );
	}

	free(data);
	if ( sampleId > 1 ) return 1;
	    

	return 1;
    }
    return 0;
}

Mp4TextTrack::~Mp4TextTrack()
{
        std::string curline;

        if ( textfile >= 0 )
        {
                // If there is an active text file, write it before closing
                encoder.GetCurrentLine(curline);
                if ( curline.length() > 0 )
                	write( textfile, curline.c_str(), curline.length() );
        }
}

mp4recorder::mp4recorder(void * ctxdata, MP4FileHandle mp4, bool waitVideo)
{
    this->ctxdata = ctxdata;
    this->mp4 = mp4;
    textSeqNo = 0;
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

		// Extract or generate timing INFO
		if ( ast_test_flag( f, AST_FRFLAG_HAS_TIMING_INFO) )
		{
			text_ts = f->ts ;
		}
		else
		{
			text_ts = getDifTime(&firstframets)/1000 ;
		}

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


int mp4player::OpenTrack(AudioCodec::Type outputCodecs[], unsigned int nbCodecs, AudioCodec::Type prefCodec, bool cantranscode )
{
	
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
	MediaFrame * f;
	unsigned long wait = 0;
	int ret;
	
	MediaFrame * f = p2->GetNextFrame(ret, wait);
	
	if ( ret >= 0 )
	{		
		if ( f->HasRtpPacketizationInfo() )
		{
			MediaFrame::RtpPacketizationInfo pinfo = f->GetRtpPacketizationInfo();
			struct ast_frame f2;
			if ( ! MediaFrameToAstFrame(f, &f2) )
			{
				return -5; /* incompatible codec read from MP4 file or unsupported media */
			}
			
			for( MediaFrame::RtpPacketizationInfo::iterator it = pinfo.begin() ;
				 it != pinfo.end ;
				 it++ )
			
			{
				MediaFrame::RtpPacketization & rtp = *it;
				f2.data = f->GetData() + rtp.GetPos();
				f2.datalen = rtp.GetSize();
				if ( rtp.IsMark() ) f2.subclass |= 1;
				
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
