#include <mp4v2/mp4v2.h>


#ifdef __cplusplus

#include <medkit/transcoder.h>
#include <medkit/audio.h>

class Mp4Basetrack
{
public:
    /**
     *  Constructor to use when creating a new track on an MP4 file
     */
    Mp4Basetrack(MP4FileHandle mp4, unsigned long initialDelay) 
    { 
        this->mp4 = mp4;
	sampleId = 0;
	mediatrack = MP4_INVALID_TRACK_ID;
	hinttrack = MP4_INVALID_TRACK_ID;
	this->initialDelay = initialDelay;
	reading = false;
    }

    virtual ~Mp4Basetrack() {};
    
    virtual int Create(const char * trackName, int codec, DWORD bitrate) = 0;
    virtual int ProcessFrame( const MediaFrame * f ) = 0;
    
    inline bool IsOpen() { return ( mediatrack != MP4_INVALID_TRACK_ID ); }
    void SetInitialDelay(unsigned long delay) { initialDelay = delay; }
    void IncreateInitialDelay(unsigned long delay) { initialDelay = initialDelay + delay; }
    bool IsEmpty() { return (sampleId == 0); }

protected:
    /**
     *  Constructor to use when reading an existing track on an MP4 file
     */
    Mp4Basetrack(MP4FileHandle mp4, MP4TrackId trackId);
    
protected:
     MP4FileHandle mp4;		
     MP4TrackId mediatrack;
     MP4TrackId hinttrack;
     int sampleId;
     unsigned long initialDelay;
     
     DWORD prevts;
     bool reading;
};


class H264Depacketizer;

#define MP4_AUDIO_TRACK		0
#define MP4_VIDEO_TRACK		1
#define MP4_VIDEODOC_TRACK	2
#define MP4_TEXT_TRACK  	3

/**
 *  Class that records a media stream into an MP4 file
 */
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
     * @param codec: codec to use
     * @param trackName: name of MP4 track to create.
     * @param textfile: file descriptor
     **/
    int AddTrack(TextCodec::Type codec, const char * trackName, int textfile);
    
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
    void * GetCtxData() { return ctxdata; }
    
    void SetParticipantName( const char * name )
    {
        strncpy( partName, name, sizeof(partName) );
	partName[sizeof(partName)-1] = 0;
    }
    
    /**
     *  Set the initial time offset to add when starting to record the media
     *  THis allows to create a participant that comes after some time in the
     *  recorded conversation
     *  @param delay: delay in ms
     */
    
    void SetInitialDelay(unsigned long delay);

    /**
     * Return whether video has started for this recorder
     *
     * @return 2: we were not waiting for video
     * 	       1: video has started
     *         0: still waiting for vide
     *        -1: this recorder does not record video
     */
     int IsVideoStarted();


     void SetWaitForVideo( bool wait ) { waitVideo = wait; }
private:
    char partName[80];
    
    MP4FileHandle mp4;
    Mp4Basetrack * mediatracks[5];
        
    int length;

    struct VideoTranscoder *vtc;
    void * ctxdata;
    
    AudioEncoder * audioencoder;

    bool waitVideo;
    unsigned long initialDelay;
    
    DWORD textSeqNo;
    DWORD videoSeqNo;
    
    BYTE audioBuff[800];
    
    H264Depacketizer * depak;
    
    // In case we need to generate a clock
    timeval firstframets;
};

/**
 *  Class plays an MP4 file
 */

class mp4player 
{
public:
    mp4player(void * ctxdata, MP4FileHandle mp4);
    ~mp4player();

    int OpenTrack(AudioCodec::Type outputCodecs[], unsigned int nbCodecs, bool cantranscode );
    int OpenTrack(VideoCodec::Type outputCodecs[], unsigned int nbCodecs, bool cantranscode, bool secondary = false );
   
   /**
    *  @param c: text codec to use
    *  @param rendering : 0 = render as subtitles, 1 = render as realtime text, 2= render as video
    */
    int OpenTrack(TextCodec::Type c, int rendering);
    
    /**
     *  Obtain the next frame to play and the time to wait after having pushed the file.
     */
    int GetNextFrame( struct ast_frame * f, unsigned long & waittime );

    int Rewind();
    
private:
    void * ctxdata;
    Mp4Basetrack * mediatracks[5];
};

#endif

#ifdef __cplusplus
extern "C"
{
#endif
    struct mp4rec;
    struct mp4play;

/**
 * Create one MP4 recording or playing session for a given asterisk channel
 * @param chan: asterisk channel that will be recorded
 * @param mp4: MP4 file handle (see MP4V2 lib) to use for recording. Must already be OPEN in the proper mode
 *
 * @param waitVideo: if true, no media will be recorded before the first valid I frame is recieved. If channel
 * does not support video, this flag is ignored.
 *
 * @param video format specification for transcoder
 * @param textfile: file discriptor for a text file to record  
 * @return MP4 participant context for recording.
 */
    struct mp4rec * Mp4RecorderCreate(struct ast_channel * chan, MP4FileHandle mp4, bool waitVideo, const char * videoformat, const char * partName, int textfile);

/**
 * Process one ast_frame and record it into the MP4 file. Warning: packets must be reordered
 * before being posted to the recorder.
 *
 * @param r: instance of mp4 recorder
 * @param f: ast_frame to record.
 **/
    int Mp4RecorderFrame( struct mp4rec * r, struct ast_frame * f );

/**
 * Return whether the recorder has started recording video. This is useful when a mp4 recorder
 * is created with the waitVideo flag set to true.
 *
 * @param r: instance of mp4 recorder
 * @return :  2 - we were not waiting for video
 *            1 - video has started
 *            0 - we are still waiting for the first I frame
 *           -1 - video is not expected by this recorder
 **/

    int Mp4RecorderHasVideoStarted( struct mp4rec * r );
    
    void Mp4RecorderSetInitialDelay( struct mp4rec * r, unsigned long ms);
    /**
     *  destoy one instance of mp4 recorder
     *  
     *  @param r: instance of mp4 recorder
     */
    void Mp4RecorderDestroy( struct mp4rec * r );
 
    struct mp4play * Mp4PlayerCreate(struct ast_channel * chan, MP4FileHandle mp4, bool transcodeVideo, int renderText);

    int Mp4PlayerPlayNextFrame(struct ast_channel * chan, struct mp4play * p);
    
    void Mp4PlayerDestroy( struct mp4play * p );
#ifdef __cplusplus
}
#endif
