#ifndef _MP4READER_H_
#define _MP4READER_H_

#include <mp4v2/mp4v2.h>

#ifdef __cplusplus

#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/avcdescriptor.h>

class Mp4Basetrack;
class RTPRedundantEncoder;

#ifndef MP4_AUDIO_TRACK
#define MP4_AUDIO_TRACK		0
#define MP4_VIDEO_TRACK		1
#define MP4_VIDEODOC_TRACK	2
#define MP4_TEXT_TRACK  	3
#endif

/**
 *  Class that plays an MP4 file.
 *  This class is independent of Asterisk : it only produces medkit MediaFrame
 *  objects. The Asterisk binding (writing ast_frame to a channel) lives in
 *  astmedkit/mp4format.h (Mp4PlayerCreate / Mp4PlayerPlayNextFrame).
 */
class mp4reader
{
public:
    mp4reader(void * ctxdata, MP4FileHandle mp4);
    virtual ~mp4reader();

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

	bool GetCodec(AudioCodec::Type & codec) const;
	bool GetVideoCodec(VideoCodec::Type & codec) const;

    // Groupe 1 — Métadonnées
    bool HasAudioTrack()      { return mediatracks[MP4_AUDIO_TRACK] != NULL; }
    bool HasVideoTrack()      { return mediatracks[MP4_VIDEO_TRACK] != NULL; }
    bool HasTextTrack()       { return mediatracks[MP4_TEXT_TRACK]  != NULL; }
    double GetDuration();
    DWORD GetVideoWidth();
    DWORD GetVideoHeight();
    DWORD GetVideoBitrate();
    double GetVideoFramerate();
    AVCDescriptor* GetAVCDescriptor();

    // Groupe 2 — Seek / position
    QWORD Seek(QWORD timeMs);
    QWORD PreSeek(QWORD timeMs);
    QWORD Tell()              { return currentTs; }

protected:
    //MP4TrackId IterateTracks(int trackIdx, const char * trackType, bool useHint = true);
	bool GetNextTrackAndTs(int & trackId, QWORD & ts);

private:
    void * ctxdata;
    Mp4Basetrack * mediatracks[5];
	QWORD next[5];
	QWORD nextBOMorRepeat;
    MP4FileHandle mp4;

    RTPRedundantEncoder * redenc;

	timeval startPlaying;
    QWORD currentTs;
};

#endif /* __cplusplus */

#endif /* _MP4READER_H_ */
