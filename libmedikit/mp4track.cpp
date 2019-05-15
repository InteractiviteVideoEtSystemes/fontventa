#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"
#include "h264/h264.h"
#include "h264/h264depacketizer.h"
#include "aac/aacconfig.h"
#include "mp4track.h"
#include "medkit/audiosilence.h"

Mp4Basetrack::Mp4Basetrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack)
{
	this->mp4 = mp4;
	this->mediatrack = mediaTrack;
	this->hinttrack = hintTrack;
	reading = true;
	this->initialDelay = 0;
	sampleId = 1;
	
	if ( hintTrack != MP4_INVALID_TRACK_ID )
	    timeScale = MP4GetTrackTimeScale(mp4, hintTrack);
	else
	    timeScale = MP4GetTrackTimeScale(mp4, mediaTrack);

	frame = NULL;
	numHintSamples = 0;
	totalDuration = 0;
}


Mp4Basetrack::Mp4Basetrack(MP4FileHandle mp4, unsigned long initialDelay) 
{ 
	this->mp4 = mp4;
	sampleId = 0;
	mediatrack = MP4_INVALID_TRACK_ID;
	hinttrack = MP4_INVALID_TRACK_ID;
	this->initialDelay = initialDelay;
	reading = false;
	frame = NULL;
}

	
QWORD Mp4Basetrack::GetNextFrameTime()
{
    QWORD ts;

    if (hinttrack != MP4_INVALID_TRACK_ID)
    {
	ts = MP4GetSampleTime(mp4, hinttrack, sampleId);
        if (ts==MP4_INVALID_TIMESTAMP)
	    return ts;
        ts = MP4ConvertFromTrackTimestamp(mp4, hinttrack, ts, 1000);
    }
    else
    {
	ts = MP4GetSampleTime(mp4, mediatrack, sampleId);
        if (ts==MP4_INVALID_TIMESTAMP)
	    return ts;
        ts = MP4ConvertFromTrackTimestamp(mp4, mediatrack, ts, 1000);
    }

    return ts;
}

const MediaFrame * Mp4Basetrack::ReadFrame()
{
    if (hinttrack != MP4_INVALID_TRACK_ID)
		return ReadFrameFromHint();
    else
		return ReadFrameWithoutHint();
}

const MediaFrame * Mp4Basetrack::ReadFrameFromHint()
{
    int last = 0;
	uint8_t* data;
	bool isSyncSample;
	//unsigned int numHintSamples;
	unsigned short packetIndex;
	unsigned int frameSamples;
	int frameSize;
	int frameTime;
	int frameType;

	frame->ClearRTPPacketizationInfo();
	
	if ( frame == NULL )
	{
	    Error("Buffer not initialized. Cannot read sample.\n");
	    return NULL;
	}
	
	// Get number of rtp packets for this sample
	if (!MP4ReadRtpHint(mp4, hinttrack, sampleId, &numHintSamples))
	{
		//Print error
		Error("Error reading hint track ID %d\n", hinttrack);
		//Exit
		return NULL;
	}

	// Get number of samples for this sample
	frameSamples = MP4GetSampleDuration(mp4, hinttrack, sampleId);

	// Get size of sample
	frameSize = MP4GetSampleSize(mp4, hinttrack, sampleId);

	// Get sample timestamp
	frameTime = MP4GetSampleTime(mp4, hinttrack, sampleId);
	
	//Convert to miliseconds
	frameTime = MP4ConvertFromTrackTimestamp(mp4, hinttrack, frameTime, 1000);

	//Get max data lenght
	DWORD dataLen = 0;
	MP4Timestamp	startTime;
	MP4Duration	duration;
	MP4Duration	renderingOffset;

	//Get values
	data	= frame->GetData();
	dataLen = frame->GetMaxMediaLength();
		
	// Read next frame packet only to get the information.
	// data will be reread using hint track
	if (!MP4ReadSample(
		mp4,				// MP4FileHandle hFile
		mediatrack,				// MP4TrackId hintTrackId
		sampleId,			// MP4SampleId sampleId,
		(u_int8_t **) &data,		// u_int8_t** ppBytes
		(u_int32_t *) &dataLen,		// u_int32_t* pNumBytes
		&startTime,			// MP4Timestamp* pStartTime
		&duration,			// MP4Duration* pDuration
		&renderingOffset,		// MP4Duration* pRenderingOffset
		&isSyncSample			/* bool* pIsSyncSample */ ))
	{
		Error("Error reading sample ID %d on track %d.\n", sampleId, mediatrack);
		//Last
		return NULL;
	}

	//Set lenght & duration
	frame->SetLength(dataLen);
	frame->SetDuration(duration/timeScale);
	VideoFrame *video = NULL;

	//Set media specific properties
	switch ( frame->GetType() )
	{
	    case MediaFrame::Video:
	        video = (VideoFrame*)frame;
			frame->SetTimestamp(startTime*90000/timeScale);
			video->SetIntra(isSyncSample);
			break;
		
	    case MediaFrame::Audio:
	       frame->SetTimestamp(startTime*8000/timeScale);
	       break;
	       
	    case MediaFrame::Text:
	       // Never used as read method is be overriden for text tracks
	       break;
	}

	BYTE * rtpdata;
	u_int32_t pos = 0;
	
	frame->SetLength(0);
	
	// read each frame's packet and build a packetized frame
	for (packetIndex = 0; packetIndex < numHintSamples; packetIndex++ )
	{
	    u_int32_t rtpLen = 0;
		
		rtpdata = NULL;
	    bool last = ( packetIndex+1 == numHintSamples );
	    
	    if ( !MP4ReadRtpPacket(
		    mp4,				// MP4FileHandle hFile
		    hinttrack,				// MP4TrackId hintTrackId
		    packetIndex,			// u_int16_t packetIndex
		    (u_int8_t **) &rtpdata,		// u_int8_t** ppBytes
		    &rtpLen,					// u_int32_t* pNumBytes
		     0,				// u_int32_t ssrc DEFAULT(0)
		     0,				// bool includeHeader DEFAULT(true)
		     1				// bool includePayload DEFAULT(true) 
		     ))
	    {
			//Error
			Error("Error reading packet from hinttrack ID %d mediatrack %d index %d]\n", hinttrack, mediatrack,packetIndex);
			//Exit
			return NULL;
	    }

		if (rtpdata != NULL && rtpLen > 0)
		{
			frame->AppendMedia(rtpdata, rtpLen);
			frame->AddRtpPacket(pos, rtpLen , NULL, 0, last);
			pos += rtpLen;
			free(rtpdata);
		}
	}

	sampleId++;

	return frame;
}

const MediaFrame * Mp4Basetrack::ReadFrameWithoutHint()
{
    	int last = 0;
	uint8_t* data;
	bool isSyncSample;
	//unsigned int numHintSamples;
	unsigned short packetIndex;
	unsigned int frameSamples;
	int frameSize;
	int frameTime;
	int frameType;

	if ( frame == NULL )
	{
	    Error("Buffer not initialized. Cannot read sample.\n");
	    return NULL;
	}
	
	// Get number of rtp packets for this sample
	if (!MP4ReadRtpHint(mp4, hinttrack, sampleId, &numHintSamples))
	{
		//Print error
		Error("Error reading hint track ID %d\n", hinttrack);
		//Exit
		return NULL;
	}

	// Get number of samples for this sample
	frameSamples = MP4GetSampleDuration(mp4, mediatrack, sampleId);

	// Get size of sample
	frameSize = MP4GetSampleSize(mp4, mediatrack, sampleId);

	// Get sample timestamp
	frameTime = MP4GetSampleTime(mp4, mediatrack, sampleId);
	
	//Convert to miliseconds
	frameTime = MP4ConvertFromTrackTimestamp(mp4, mediatrack, frameTime, 1000);

	//Get max data lenght
	DWORD dataLen = 0;
	MP4Timestamp	startTime;
	MP4Duration	duration;
	MP4Duration	renderingOffset;

	//Get values
	data	= frame->GetData();
	dataLen = frame->GetMaxMediaLength();
		
	// Read next frame packet
	if (!MP4ReadSample(
		mp4,				// MP4FileHandle hFile
		mediatrack,				// MP4TrackId hintTrackId
		sampleId,			// MP4SampleId sampleId,
		(u_int8_t **) &data,		// u_int8_t** ppBytes
		(u_int32_t *) &dataLen,		// u_int32_t* pNumBytes
		&startTime,			// MP4Timestamp* pStartTime
		&duration,			// MP4Duration* pDuration
		&renderingOffset,		// MP4Duration* pRenderingOffset
		&isSyncSample			/* bool* pIsSyncSample */ ))
	{
		Error("Error reading sample ID %d on track %d.\n", sampleId, mediatrack);
		//Last
		return NULL;
	}

	//Set lenght & duration
	frame->SetLength(dataLen);
	frame->SetDuration(duration/timeScale);
	//frame->SetDuration(duration);
	VideoFrame *video = NULL;

	//Set media specific properties
	switch ( frame->GetType() )
	{
	    case MediaFrame::Video:
	        video = (VideoFrame*)frame;
			frame->SetTimestamp(startTime*90000/timeScale);
			video->SetIntra(isSyncSample);
			break;
		
	    case MediaFrame::Audio:
	       frame->SetTimestamp(startTime*8000/timeScale);
	       break;
	       
	    case MediaFrame::Text:
	       // Never used as read method will be overrienden for text tracks
	       break;
	}
	
	sampleId++;

	return frame;
}

int Mp4AudioTrack::Create(const char * trackName, int codec, DWORD samplerate)
{
    // Create audio track
    AACSpecificConfig config(samplerate,1);
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
		
	case AudioCodec::SLIN:
		mediatrack =  MP4AddAudioTrack(mp4, 8000, 160, MP4_PCM16_BIG_ENDIAN_AUDIO_TYPE);
		// Create audio hint track
		hinttrack = MP4AddHintTrack(mp4, mediatrack);
		// Set payload type for hint track
		type = 0;
		MP4SetHintTrackRtpPayload(mp4, hinttrack, "PCMU", &type, 0, NULL, 1, 0);
		 // Set channel and sample properties
		MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.slin.channels", 1);
		MP4SetTrackIntegerProperty(mp4, mediatrack, "mdia.minf.stbl.stsd.slin.sampleSize", 16);
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
	
	
	case AudioCodec::AAC:
	    // TODO: complete AAC support
	    mediatrack = MP4AddAudioTrack(mp4, samplerate, 1024, MP4_MPEG2_AAC_LC_AUDIO_TYPE);
	    // Create no hint track
        
       	    // Set channel and sample properties
	    MP4SetTrackESConfiguration(mp4, mediatrack, config.GetData(),config.GetSize());
	    break;

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
			for ( DWORD d = 0; d < initialDelay; d += 20 )
			{
			    if ( initialDelay - d > 20 )
				duration = 20;
			    else
				duration = initialDelay - d;
			     duration = duration*f2->GetRate()/1000;
			     MP4WriteSample(mp4, mediatrack, silence->GetData(), silence->GetLength(), duration, 0, 1 );
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
	    
			}
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
		totalDuration += duration / ( f2->GetRate()/1000 ) ;
		
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


int Mp4VideoTrack::DoWritePrevFrame(DWORD duration)
{
	
	sampleId++;
	//Log("Process VIDEO frame sampleId: %d, ts:%lu, duration %u.\n", sampleId, frame->GetTimeStamp(), duration);
	
	MP4WriteSample(mp4, mediatrack, frame->GetData(), frame->GetLength(), duration, 0, ((VideoFrame *) frame)->IsIntra());

	//Check if we have rtp data
	if (frame->HasRtpPacketizationInfo())
	{
		//Get list
		MediaFrame::RtpPacketizationInfo& rtpInfo = frame->GetRtpPacketizationInfo();
		//Add hint for frame
		MP4AddRtpHint(mp4, hinttrack);
		//Get iterator
		MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin();
		
		for (it = rtpInfo.begin(); it != rtpInfo.end(); it++)
		{
			MediaFrame::RtpPacketization * rtp = *it;

			if ( ((VideoFrame *) frame)->GetCodec()==VideoCodec::H264 )
			{
				//Get rtp data pointer
				BYTE *data = frame->GetData()+rtp->GetPos();
				//Check nal type
				BYTE nalType = data[0] & 0x1F;
				//Get nal data
				BYTE *nalData = data+1;
				DWORD nalSize = rtp->GetSize()-1;

				//If it a SPS NAL
				if (nalType==0x07)
				{
					H264SeqParameterSet sps;
					
					try
					{
						//DEcode SPS
						sps.Decode(nalData,nalSize);
					}
					catch (std::runtime_error e)
					{
						Error("Failed to decode SPS packet. Skipping it.\n");
						continue;
					}
					
					//Dump
					//sps.Dump();
					// Update size
					width = sps.GetWidth();
					height = sps.GetHeight();
					
					if (!hasSPS)
					{
						//Add it
						MP4AddH264SequenceParameterSet(mp4,mediatrack,data,rtp->GetSize());
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
						
					}
					//continue;
				}

				//If it is a PPS NAL
				if (nalType==0x08)
				{
					if (!hasPPS)
					{
						//Add it
						MP4AddH264PictureParameterSet(mp4,mediatrack,data,rtp->GetSize());
						//No need to search more
						hasPPS = true;
					}
					//continue;
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
		MP4WriteRtpHint(mp4, hinttrack, duration, ((VideoFrame *) frame)->IsIntra());
	}
	return 0;
}

int Mp4VideoTrack::ProcessFrame( const MediaFrame * f )
{
    if ( f->GetType() == MediaFrame::Video )
    {
        VideoFrame * f2 = ( VideoFrame *) f;
		DWORD duration;
		if (f2->GetCodec() != (VideoCodec::Type) codec )
		{	
			// invalid codec
			return -2;
		}
		
		// If video is not started, wait for the first I Frame
		if ( ! videoStarted )
		{
			if ( f2->IsIntra() )
			{
				Log("-mp4recorder: got the first I frame. Video recording starts on track id:%d.\n", mediatrack);
				videoStarted = true;
			}
			else
			{
				// drop all the frames until an I-Frame is detected
				return 0;
			}
		}
	
	
		if (frame == NULL)
		{
			// First frame: record frame when we will have the next one to compute duration.
			frame = f2->Clone();
			return 1;
		}
		else
		{
			if ( frame->GetTimeStamp() < f2->GetTimeStamp() )
			{
				duration = f2->GetTimeStamp() - frame->GetTimeStamp();
			}
			else
			{
				// default to 20 fps
				duration = 50*90;
				Log("Inconsistent video TS %ld >= %ld. Using defulat duration of %u.\n", 
				     frame->GetTimeStamp(), f2->GetTimeStamp(), duration);
			}
			
			if ( initialDelay > 0 && sampleId == 0)
			{
				duration = initialDelay*90;
				Log("Adding %d ms of initial delay on video track id:%d.\n", initialDelay, mediatrack);			
			}
		}
		
		// Write previous frame in file
		DoWritePrevFrame(duration);
		totalDuration += duration/90;

		//Save current frame as previous frame
		delete frame;
		frame = f2->Clone();
		
		return 1;
	}
	else
	{
		// invalid  media
	    return -1;
	}
}

const MediaFrame * Mp4VideoTrack::ReadFrame()
{
	VideoFrame * vf = (VideoFrame *) Mp4Basetrack::ReadFrame();
	if (paramFrame != NULL && vf->IsIntra())
	{
		// H.264 If frame is an intra prepend the SEQ / PICT headers
		if ( ! vf->PrependWithFrame(paramFrame) )
		{
			Error("Failed to add H.264 seq / pict params\n");
		}
	}
	
	return vf;
}

VideoFrame * Mp4VideoTrack::ReadH264Params()
{
	uint8_t **seqheader, **pictheader = NULL;
	uint32_t *pictheadersize, *seqheadersize = NULL;
	uint32_t ix;
	
	VideoFrame * vf = new VideoFrame(codec, 5000, true);
	
	if (vf == NULL)
	{
		Error("Failed to allocate VideoFrame form H264 parameters.\n");
		return NULL;
	}
	
	MP4GetTrackH264SeqPictHeaders(mp4, mediatrack, 
                                 &seqheader, &seqheadersize,
                                 &pictheader, &pictheadersize);
	
	
	if (seqheader != NULL && seqheadersize != NULL)
	{
		for (ix = 0; seqheadersize[ix] != 0; ix++)
		{
			DWORD pos = vf->GetLength();
			vf->AppendMedia(seqheader[ix], seqheadersize[ix]);
			vf->AddRtpPacket(pos, seqheadersize[ix], NULL, 0, false);
		
			if (seqheader[ix]) free(seqheader[ix])	;		
		}
		Debug("Read %u sequence parameter header(s) from H.264 video track ID %d.\n", ix, mediatrack);
	}
	else
	{
		Error("No H.264 sequence parameter found video track ID %d.\n", mediatrack);
	}

	if (pictheadersize != NULL && pictheader != NULL)
	{
		for (ix = 0; pictheadersize[ix] != 0; ix++)
		{
			DWORD pos = vf->GetLength();
			vf->AppendMedia(pictheader[ix], pictheadersize[ix]);
			vf->AddRtpPacket(pos, pictheadersize[ix], NULL, 0, false);
			if (pictheader[ix]) free(pictheader[ix]);
		}     
		Debug("Read %u picture parameter header(s) from H.264 video track ID %d.\n", ix, mediatrack);
	}
	else
	{
		Error("No H.264 picture parameter found video track ID %d.\n", mediatrack);
	}

	if (pictheader) free(pictheader);
	if (pictheadersize) free(pictheadersize);

	if ( vf->GetLength() == 0 )
	{
		Error("Failed to load any H.264 sequence or picture parameters.\n");
		delete vf;
		vf = NULL;
	}
	return vf;
}

Mp4TextTrack::Mp4TextTrack(MP4FileHandle mp4, MP4TrackId mediaTrack) : Mp4Basetrack(mp4, mediaTrack, MP4_INVALID_TRACK_ID) 
{
    Log("-mp4recorder: creating subtitle to TTR renderer.\n"); 
    conv1 = new SubtitleToRtt();
    textfile = -1;
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
			//Set size
			silence[0] = 0;
			silence[1] = 0;
			MP4WriteSample( mp4, mediatrack, silence, 2, initialDelay, 0, true );
			Log("Adding %d ms of initial delay on text track id:%d.\n", initialDelay, mediatrack);
		}
		frameduration = 100; 
	}
	else
	{
	    frameduration = (f2->GetTimeStamp()-prevts);
	}

	//Log("Process TEXT frame  ts:%lu, duration %lu [%ls].\n",  f2->GetTimeStamp(), frameduration, f2->GetWString().c_str());
	//Log("Process TEXT frame  ts:%lu, duration %lu. sampleId=%d\n",  f2->GetTimeStamp(), frameduration, sampleId );
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
		int ret = ::write( textfile, subtitle.data(), subtitle.length() );
		savedText += subtitle;
	    }
	}
	
	encoder.GetSubtitle(subtitle);
	unsigned int subsize = subtitle.length();

	
	BYTE* data = (BYTE*)malloc(subsize+2);

	//Set size
	data[0] = subsize>>8;
	data[1] = subsize & 0xFF;
	
	memcpy(data+2, subtitle.data(), subsize);
	    
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

	sampleId++;

	free(data);
	if ( sampleId > 1 ) return 1;
	    

	return 1;
    }
    return 0;
}

const MediaFrame * Mp4TextTrack::ReadFrame()
{
	int next = 0;
	int last = 0;
	int first = 0;

	u_int32_t dataLen;
	unsigned int frameSamples;
	int frameTime;
	int frameType;
	MP4Timestamp	startTime;
	MP4Duration	duration;
	MP4Duration	renderingOffset;

	// Get number of samples for this sample
	frameSamples = MP4GetSampleDuration(mp4, mediatrack, sampleId);

	// Get size of sample
	dataLen = MP4GetSampleSize(mp4, mediatrack, sampleId);

	// Get sample timestamp
	frameTime = MP4GetSampleTime(mp4, mediatrack, sampleId);
	//Convert to miliseconds
	frameTime = MP4ConvertFromTrackTimestamp(mp4, mediatrack, frameTime, 1000);

	if ( dataLen <= sizeof(buffer))
	{
		u_int8_t * buf2 = buffer;
		// Get data pointer
		//Get max data lenght
		
		// Read next rtp packet
		if (!MP4ReadSample(
				mp4,				// MP4FileHandle hFile
				mediatrack,				// MP4TrackId hintTrackId
				sampleId++,			// MP4SampleId sampleId,
				(u_int8_t **) &buf2,		// u_int8_t** ppBytes
				(u_int32_t *) &dataLen,		// u_int32_t* pNumBytes
				&startTime,			// MP4Timestamp* pStartTime
				&duration,			// MP4Duration* pDuration
				&renderingOffset,		// MP4Duration* pRenderingOffset
				NULL				// bool* pIsSyncSample
			))
			//Last
			return NULL;

		//Log("Got text frame [time:%d,start:%d,duration:%d,lenght:%d,offset:%d\n",frameTime,startTime,duration,dataLen,renderingOffset);
		//Dump(data,dataLen);
		//Get length
		if (dataLen>2)
		{
			uint32_t len2 = dataLen;
			//Get string length
			u_int32_t origLen = dataLen;
			dataLen = buffer[0]<<8 | buffer[1];
			if (dataLen > sizeof(buffer) ) 
			{
				Log("Subtitle stored length %d is too long. OrigLen=%d\n", dataLen,len2);
				dataLen = len2;
			}
			//Set frame
			//frame->SetFrame(startTime,data+2+renderingOffset,len-renderingOffset-2);
		}
		else
		{
			dataLen = 0;
		}
	}
	else
	{
		Log("Subtitle too long.\n");
		dataLen = 0;
	}
		
	if ( frame == NULL) frame = new TextFrame(true);		

	if (dataLen > 0)
	{
	    // If SubtitleToRtt converter is allocated, it means that we
	    // need to compute the difference between the previous subtile and
	    // the current one in order to render this as real time text
	    TextFrame * tf = (  TextFrame * ) frame;

	    if ( conv1 != NULL )
	    {	
		std::string txtsample((const char *) &buffer[2+renderingOffset], dataLen - renderingOffset);
		std::string rttstr;
		unsigned int nbdel = 0;
	
		//Debug("mp4play: read subtitle %s. renderingOffset=%d, len=%d.\n",
		//	txtsample.c_str(), renderingOffset, dataLen - renderingOffset);		
		conv1->GetTextDiff(txtsample, nbdel, rttstr);
		if (nbdel > 0) rttstr.insert(0, 0x08, nbdel);
			
		if ( frame->Alloc( rttstr.length()) )
		{
			tf->SetFrame(startTime, (const BYTE *)rttstr.data(), rttstr.length() );
			frame->ClearRTPPacketizationInfo();
			if (nbdel > 0)
			{
				frame->AddRtpPacket(0, nbdel, NULL, 0, true);
			}
				
			frame->AddRtpPacket(nbdel, rttstr.length() - nbdel, NULL, 0, true);
		}
		frame->SetTimestamp(startTime);
	    }
	    else
	    {
			tf->SetFrame(startTime, buffer + 2 + renderingOffset, dataLen - renderingOffset);
			frame->AddRtpPacket(0, renderingOffset, NULL, 0, true);
	    }
	}
	else
	{
		frame->SetLength(0);
	}

	frame->SetTimestamp(startTime);
	return frame;
}



Mp4TextTrack::~Mp4TextTrack()
{
        std::string curline;

        if ( textfile >= 0 )
        {
                // If there is an active text file, write it before closing
                encoder.GetCurrentLine(curline);
                if ( curline.length() > 0 )
		{
                	write( textfile, curline.data(), curline.length() );
			write( textfile, "\r\n", 2);
		}
        }
	
	if (conv1) delete conv1;
}
