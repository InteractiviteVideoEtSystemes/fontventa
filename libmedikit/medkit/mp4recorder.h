#ifndef _MP4RECORDER_H_
#define _MP4RECORDER_H_

#include <mp4v2/mp4v2.h>

#ifdef __cplusplus

#include <medkit/audio.h>
#include <medkit/video.h>

class Mp4Basetrack;
class H264Depacketizer;
class RTPRedundantEncoder;
class PictureStreamer;
struct VideoTranscoder;

#define MP4_AUDIO_TRACK		0
#define MP4_VIDEO_TRACK		1
#define MP4_VIDEODOC_TRACK	2
#define MP4_TEXT_TRACK  	3

/**
 *  Class that records a media stream into an MP4 file.
 *  This class is independent of Asterisk : it only deals with medkit MediaFrame
 *  objects. The Asterisk binding (recording ast_frame) lives in astmedkit/mp4format.h
 *  (class AstMp4Recorder).
 */
class mp4recorder
{
public:
    mp4recorder(void * ctxdata, MP4FileHandle mp4, bool waitVideo);
    virtual ~mp4recorder();

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
	void SaveTextInComment( bool save ) { saveTxtInComment = save; }

	/**
	 * Add black frames until the delay is reached and the first I frame is received
	 */
	void EnableVideoPrologue(bool prologueEnabled) { addVideoPrologue = prologueEnabled; }

	void Flush();

    void DumpInfo();

protected:
    char partName[80];

    MP4FileHandle mp4;
    Mp4Basetrack * mediatracks[5];

    int length;

    struct VideoTranscoder *vtc;
    void * ctxdata;

    AudioEncoder * audioencoder;

    int waitVideo;
    bool waitNextVideoFrame;
    unsigned long initialDelay;
	QWORD videoDelay;

    DWORD textSeqNo;
    DWORD videoSeqNo;

    BYTE audioBuff[800];

    H264Depacketizer * depak;

    // In case we need to generate a clock
    timeval firstframets;
	timeval lastfur;

	// to generate video prologue
	PictureStreamer * pcstream;

	bool saveTxtInComment;
	bool addVideoPrologue;
};

#endif /* __cplusplus */

#endif /* _MP4RECORDER_H_ */
