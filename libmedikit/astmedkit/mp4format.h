#include <mp4v2/mp4v2.h>

#ifdef __cplusplus

#include <medkit/transcoder.h>
#include <medkit/audio.h>

class Mp4Basetrack;
class H264Depacketizer;
class RTPRedundantEncoder;

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
	
	void Flush();
	
private:
    char partName[80];
    
    MP4FileHandle mp4;
    Mp4Basetrack * mediatracks[5];
        
    int length;

    struct VideoTranscoder *vtc;
    void * ctxdata;
    
    AudioEncoder * audioencoder;

    bool waitVideo;
    bool waitNextVideoFrame;
    unsigned long initialDelay;
	QWORD videoDelay;
    
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

    int OpenTrack(AudioCodec::Type outputCodecs[], unsigned int nbCodecs, AudioCodec::Type prefCodec, bool cantranscode );
    int OpenTrack(VideoCodec::Type outputCodecs[], unsigned int nbCodecs, VideoCodec::Type prefCodec, bool cantranscode, bool secondary = false );
   
   /**
    *  @param c: text codec to use
    *  @param rendering : 0 = render as subtitles, 1 = render as realtime text, 2= render as video
    */
    int OpenTrack(TextCodec::Type c, BYTE pt, int rendering);
    
    /**
     *  Obtain the next frame to play and the time to wait after having pushed the frame.
	 * DO NOT RELEASE OBTAINED MEDIA FRAME, memmory is managed by mediatrack. Rewind MUST be called after tracks are open 
	 * before calling this fonction to render frames
	 * @param now: rendering time 
	 * @param [out] errcode: error code. 0 mean no frame ready. 1=returned a frame. -2 = EOF, -3, invalid track, negative give details on failure
	 * @param [out] waittime: time to wait before the next frame. Can be 0. In this case 
	 * @return NULL or the next frame to render
     */
    MediaFrame * GetNextFrame( int & errcode, unsigned long & waittime );

	
	/**
	 * Reset MP4 stream to read
	 **/
    int Rewind();
    
    bool Eof();

    BYTE buffer[2000];

protected:
    //MP4TrackId IterateTracks(int trackIdx, const char * trackType, bool useHint = true);
	bool GetNextTrackAndTs(int & trackId, QWORD & ts);
    
private:
    void * ctxdata;
    Mp4Basetrack * mediatracks[5];
	QWORD next[5];
    MP4FileHandle mp4;
    
    RTPRedundantEncoder * redenc;

	timeval startPlaying;
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
	
	void Mp4RecorderFlush( struct mp4rec * r );

    /**
     *  destoy one instance of mp4 recorder
     *  
     *  @param r: instance of mp4 recorder
     */
    void Mp4RecorderDestroy( struct mp4rec * r );

	/**
	 * Create an instance of MP4 player
	 * @param chan asterisk channel to associate with this player. nativeformats and writeformat needs to be correctly set
	 * @param mp4 MP4 File handle
	 * @param transcodeVideo true if video transcoding is authorized (takes more CPU)
	 * @param renderText 0 = render as subtitle (ocompelte sencences) 1 = render as realtime text, 2 = render in video (not supported yet)
	 */
    struct mp4play * Mp4PlayerCreate(struct ast_channel * chan, MP4FileHandle mp4, bool transcodeVideo, int renderText);

    int Mp4PlayerPlayNextFrame(struct ast_channel * chan, struct mp4play * p);
    
    void Mp4PlayerDestroy( struct mp4play * p );
#ifdef __cplusplus
}
#endif
