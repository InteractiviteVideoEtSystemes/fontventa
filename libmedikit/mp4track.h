#include <mp4v2/mp4v2.h>
#include "medkit/log.h"
#include "medkit/codecs.h"
#include "medkit/audio.h"
#include "medkit/video.h"
#include "medkit/text.h"
#include "medkit/textencoder.h"


class Mp4Basetrack
{
public:
    /**
     *  Constructor to use when creating a new track on an MP4 file
     */
    Mp4Basetrack(MP4FileHandle mp4, unsigned long initialDelay); 

    /**
     * Constructor to use when reading an MP4 file
     */
    Mp4Basetrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack);
	
    virtual ~Mp4Basetrack() 
    {
	if (frame)
	{
		frame->ClearRTPPacketizationInfo();
		delete frame;    
	}
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate) = 0;
    virtual int ProcessFrame( const MediaFrame * f ) = 0;
    
    inline bool IsOpen() { return ( mediatrack != MP4_INVALID_TRACK_ID ); }
    void SetInitialDelay(unsigned long delay) { initialDelay = delay; }
    void IncreateInitialDelay(unsigned long delay) { initialDelay = initialDelay + delay; }
    bool IsEmpty() { return (sampleId == 0 && frame == NULL); }
	void Reset() { sampleId = 1; }

    virtual const MediaFrame * ReadFrame();
    QWORD GetNextFrameTime();
	DWORD GetRecordedDuration() { return totalDuration; }
	int GetSampleId() { return sampleId; }
	int GetTrackId() { return mediatrack; }
	
protected:
    
    const MediaFrame * ReadFrameFromHint();
    const MediaFrame * ReadFrameWithoutHint();

protected:
    MP4FileHandle mp4;		
    MP4TrackId mediatrack;
    MP4TrackId hinttrack;
    int sampleId;
    unsigned long initialDelay;
    unsigned int timeScale;
    WORD numHintSamples;
     
    DWORD prevts;
	DWORD totalDuration;
    bool reading;
    MediaFrame * frame;
};

class Mp4AudioTrack : public Mp4Basetrack
{
public:
    Mp4AudioTrack(MP4FileHandle mp4, unsigned long delay) : Mp4Basetrack(mp4, delay) { }
    
    Mp4AudioTrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack, AudioCodec::Type codec) : Mp4Basetrack(mp4, mediaTrack, hintTrack) 
    {
	const char* nm = MP4GetTrackMediaDataName(mp4,mediaTrack);
	this->codec = codec;    
	Log("Opened audio track %s ID %d Hint %d\n", nm, mediaTrack, hintTrack);
	frame = new AudioFrame(codec,8000);
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
	
	AudioCodec::Type GetCodec() { return codec; }

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
		paramFrame = NULL;
    }

    Mp4VideoTrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack, VideoCodec::Type codec) : Mp4Basetrack(mp4, mediaTrack, hintTrack) 
    {
		this->codec = codec;
		VideoFrame * f = new VideoFrame(codec,262143);
		
		// Obtain NALU size storage size
		uint32_t naluSz_storage;
		MP4GetTrackH264LengthSize(mp4, mediaTrack, &naluSz_storage);
		f->SetH264NalSizeLength(naluSz_storage);
		frame = f;
		
		hasSPS = false;
		hasPPS = false;
		if (codec == VideoCodec::H264) 
			paramFrame = ReadH264Params();
		else
			paramFrame = NULL;
    }
	
	virtual ~Mp4VideoTrack() 
    {
		if (paramFrame) delete paramFrame;    
    }
    void SetSize(DWORD width, DWORD height)
    {
		this->width = width;
		this->height = height;
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    bool IsVideoStarted() { return videoStarted; }
    
    void WriteLastFrame()
    {
                if (frame)
                {
                        DoWritePrevFrame(50*90);
                        delete frame;
                        frame = NULL;
                }
    }
    void SetH264ProfileLevel( unsigned char profile, unsigned char constraint, unsigned char level )
    {
	AVCProfileIndication = profile;
	AVCProfileCompat = constraint;
	AVCLevelIndication = level;
    }
    
	/* Overloaded to get H.264 parameters */
	virtual const MediaFrame * ReadFrame();
	
    VideoCodec::Type GetCodec() { return codec; }

private:
	int DoWritePrevFrame(DWORD duration);
	VideoFrame * ReadH264Params();
	
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
    
	VideoFrame * paramFrame;
    VideoCodec::Type codec;
    std::string trackName;
};

#define MAX_SUBTITLE_DURATION 7000

class Mp4TextTrack : public Mp4Basetrack, TextEncoder::Listener 
{
public:
    Mp4TextTrack(MP4FileHandle mp4, int textfile, unsigned long delay) : Mp4Basetrack(mp4, delay) 
    { 
		this->textfile = textfile;
		encoder.SetListener(this);
		conv1 = NULL;
    }
    
    Mp4TextTrack(MP4FileHandle mp4, MP4TrackId mediaTrack);
    
    virtual ~Mp4TextTrack();
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    virtual const MediaFrame * ReadFrame();
    void RenderAsReatimeText(bool render);
    void GetSavedTextForVm(std::string & text) 
    { 
	encoder.GetFullText(text);
    }
    
private:
    TextEncoder encoder;
    //MP4TrackId rawtexttrack;
    int textfile;
    SubtitleToRtt * conv1;
    BYTE buffer[600];

    virtual void onNewLine(std::string & prevline);
    virtual void onLineRemoved(std::string & prevline);
};
