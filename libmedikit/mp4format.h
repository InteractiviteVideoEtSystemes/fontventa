#include "transcoder.h"
#include "framebuffer.h"


#ifdef _cplusplus
class Mp4track
{
public:
    mp4track(MP4FileHandle mp4) 
    { 
        this->mp4 = mp4;
	sampleId = 0;
	mediatrack = MP4_INVALID_TRACK_ID;
	hinttrack = MP4_INVALID_TRACK_ID;
    }

    virtual ~mp4track() {};
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate) = 0;
    virtual int ProcessFrame( const MediaFrame * f ) = 0;
    
    inline bool IsOpen() { return ( mediatrack != MP4_INVALID_TRACK_ID ); }
    
protected:
     MP4FileHandle mp4;
     MP4TrackId mediatrack;
     MP4TrackId hinttrack;
     int sampleId;
};

#define MP4_AUDIO_TRACK	0
#define MP4_VIDEO_TRACK	1
#define MP4_TEXT_TRACK  2

class mp4recorder 
{
    mp4recorder(void * ctxdata, MP4FileHandle mp4);
    ~mp4recorder();
    
    int AddTrack(int trackType, const char * trackName);
    int ProcessFrame( struct ast_frame * f );
    int ProcessFrame( const MediaFrame * f );
    void * GetCtxDate() { return ctxdata; }
    
private:
    char partName[80];
    
    MP4FileHandle mp4;
    MP4TrackId mediatracks[5];
        
    int length;

    struct VideoTranscoder *vtc;
    struct TextTranscoder  *ttc;
    void * ctxdata;
};

#endif

struct

#ifdef _cplusplus
extern "C"
{
#endif
/**
 * Create one MP4 recording or playing session for a given asterisk channel
 * @param chan: asterisk channel that will be recorded
 * @param mp4: MP4 file handle (see MP4V2 lib) to use for recording. Must already be OPEN in the proper mode
 * @param video format specification for transcoder
 * @return MP4 participant context for recording.
 */

struct mp4participant * CreateMp4Recorder(struct ast_channel * chan, MP4FileHandle mp4, char * videoformat);

/**
 * Process one ast_frame and record it into the MP4 file
 **/

 int Mp4RecordFrame( struct mp4participant * p, struct ast_frame * f );

 
#ifdef _cplusplus
}
#endif
