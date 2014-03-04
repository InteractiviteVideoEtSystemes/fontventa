#include <mp4v2/mp4v2.h>


#ifdef __cplusplus

#include <medkit/transcoder.h>
#include <medkit/audio.h>


class Mp4Basetrack
{
public:
    Mp4Basetrack(MP4FileHandle mp4) 
    { 
        this->mp4 = mp4;
	sampleId = 0;
	mediatrack = MP4_INVALID_TRACK_ID;
	hinttrack = MP4_INVALID_TRACK_ID;
    }

    virtual ~Mp4Basetrack() {};
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate) = 0;
    virtual int ProcessFrame( const MediaFrame * f ) = 0;
    
    inline bool IsOpen() { return ( mediatrack != MP4_INVALID_TRACK_ID ); }
    
protected:
     MP4FileHandle mp4;
     MP4TrackId mediatrack;
     MP4TrackId hinttrack;
     int sampleId;
     DWORD prevts;
};

#define MP4_AUDIO_TRACK		0
#define MP4_VIDEO_TRACK		1
#define MP4_VIDEODOC_TRACK	2
#define MP4_TEXT_TRACK  	3

class mp4recorder 
{
public:
    mp4recorder(void * ctxdata, MP4FileHandle mp4, bool waitVideo);
    ~mp4recorder();
    
    /**
     * Create an audio track
     **/
    int AddTrack(AudioCodec::Type codec, DWORD samplerate, const char * trackName);
    
    /**
     * Create an audio track
     **/
    int AddTrack(VideoCodec::Type codec, DWORD width, DWORD height, DWORD bitrate, const char * trackName, bool secondary = false);
    
    /**
     * Create a text track
     **/
    int AddTrack(TextCodec::Type codec, const char * trackName);
    
    /**
     * Process ONE asterisk frame and record it into the MP4 file
     *
     * @param f: asterisk frame to process
     * @param secondary: frame from a secondary media stream (for future use)
     *
     * @return 1 = frame is processed and recorded
     *         0 = frame is empty or not considered for recording
     *        -1 = frame codec does not match track codec (need to transcode)
     *        -2 = frams media does not match track media
     *        -3 = track is not open for this media
     *        -4 = this frame codec is not supported by mp4recorder
     *        -5 = could not record data (probably incorect MP4 file handle)
     **/
    int ProcessFrame( struct ast_frame * f, bool secondary = false );
    
    /**
     * Process one media frame
     * @param f: media frame to process
     * @param secondary: frame from a secondary media stream (for future use)
     *
     * @return 1 = frame is processed and recorded
     *         0 = frame is empty or not considered for recording
     *        -1 = frame codec does not match track codec (need to transcode)
     *        -2 = frame media does not match track media
     *        -3 = track is not open for this media
     *        -4 = this frame codec is not supported by mp4recorder
     *        -5 = could not record data (probably incorect MP4 file handle)
     **/
    int ProcessFrame( const MediaFrame * f, bool secondary = false );
    void * GetCtxDate() { return ctxdata; }
    
    void SetParticipantName( const char * name )
    {
        strcpy( participantName, name );
    }
    
private:
    char partName[80];
    
    MP4FileHandle mp4;
    Mp4Basetrack * mediatracks[5];
        
    int length;

    struct VideoTranscoder *vtc;
    void * ctxdata;
    
    AudioEncoder * audioencoder;

    bool waitVideo;
    
    DWORD textSeqNo;
    DWORD videoSeqNo;
    
    char participantName[8];
    BYTE audioBuff[800];
};

#endif

#ifdef __cplusplus
extern "C"
{
#endif

struct mp4rec;

/**
 * Create one MP4 recording or playing session for a given asterisk channel
 * @param chan: asterisk channel that will be recorded
 * @param mp4: MP4 file handle (see MP4V2 lib) to use for recording. Must already be OPEN in the proper mode
 * @param video format specification for transcoder
 * @return MP4 participant context for recording.
 */
struct mp4rec * Mp4RecorderCreate(struct ast_channel * chan, MP4FileHandle mp4, bool waitVideo, char * videoformat);

/**
 * Process one ast_frame and record it into the MP4 file. Warning: packets must be reordered
 * before being posted to the recorder.
 *
 * @param r: instance of mp4 recorder
 * @param f: ast_frame to record.
 **/
int Mp4RecorderFrame( struct mp4rec * r, struct ast_frame * f );

void Mp4RecorderDestroy( struct mp4rec * r );
 
#ifdef __cplusplus
}
#endif
