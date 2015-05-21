#include <mp4v2/mp4v2.h>

class SubtitleToRtt;

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
	if (frame) delete frame;    
    }
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate) = 0;
    virtual int ProcessFrame( const MediaFrame * f ) = 0;
    
    inline bool IsOpen() { return ( mediatrack != MP4_INVALID_TRACK_ID ); }
    void SetInitialDelay(unsigned long delay) { initialDelay = delay; }
    void IncreateInitialDelay(unsigned long delay) { initialDelay = initialDelay + delay; }
    bool IsEmpty() { return (sampleId == 0); }

    virtual const MediaFrame * ReadFrame();
    
protected:
    QWORD GetNextFrameTime();

protected:
     MP4FileHandle mp4;		
     MP4TrackId mediatrack;
     MP4TrackId hinttrack;
     int sampleId;
     unsigned long initialDelay;
     unsigned int timeScale;
     
     DWORD prevts;
     bool reading;
     MediaFrame * frame;
};

class Mp4AudioTrack : public Mp4Basetrack
{
public:
    Mp4AudioTrack(MP4FileHandle mp4, unsigned long delay) : Mp4Basetrack(mp4, delay) { }
    
    Mp4AudioTrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack, AudioCodec::Type codec) : Mp4Basetrack(mp4, mediaTrack, hintTrack) 
    {
	const char* nm = MP4GetTrackMediaDataName(mp4,lastHintMatch);
	this->codec = codec;    
	Log("Opened audio track %s ID %d Hint %d\n", nm, mediaTrack, hintTrack);
	frame = new AudioFrame(codec,8000);
    }
    
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

    Mp4VideoTrack(MP4FileHandle mp4, MP4TrackId mediaTrack, MP4TrackId hintTrack, VideoCodec::Type codec) : Mp4Basetrack(mp4, mediaTrack, hintTrack) 
    {
	this->codec = codec;
	frame = new VideoFrame(codec,262143);
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
	conv1 = NULL;
    }
    
    Mp4TextTrack(MP4FileHandle mp4, MP4TrackId mediaTrack);
    
    virtual ~Mp4TextTrack();
    virtual int Create(const char * trackName, int codec, DWORD bitrate);
    virtual int ProcessFrame( const MediaFrame * f );
    virtual const MediaFrame * ReadFrame();
    
private:
    TextEncoder encoder;
    MP4TrackId rawtexttrack;
    int textfile;
    SubtitleToRtt * conv1;
};
