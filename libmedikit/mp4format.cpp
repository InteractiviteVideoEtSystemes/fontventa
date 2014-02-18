#include "mp4format.h"

class Mp4AudioTrack : public mp4track
{
public:
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

class Mp4TextFrameTrack : public mp4track
{
public:
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );
    
private:

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
    return 0;
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
	if (mediatrack > 0) Log("-mp4recorder: created video track %d using codec %s.\n", mediatrack, VideoCodec::GetNameFor(codec));

}


int Mp4VideoTrack::ProcessFrame( MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Audio )
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
    }
    return 0;
}

int Mp4RecordFrame( struct mp4participant * p, struct ast_frame * f )
{
    if (f != NULL)
    {
	int ret;
	
        switch(f->frametype)
	{
	    case AST_FRAME_VOICE:
	        ret = RecordV
	    
	    case AST_FRAME_VIDEO:
	    
	    case AST_FRAME_TEXT:
}