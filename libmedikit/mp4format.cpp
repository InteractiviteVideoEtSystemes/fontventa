#include "mp4format.h"

class Mp4AudioTrack : public mp4track
{
public:
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );

};

class Mp4VideoTrack : public mp4track
{
public:
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( MediaFrame * f );
    
private:
    bool firstpkt;
    bool intratrame;
    bool waitVideo;

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
    
    return 1;
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
