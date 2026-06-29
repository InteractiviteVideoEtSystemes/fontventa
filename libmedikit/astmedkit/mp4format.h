#ifndef _MP4FORMAT_H_
#define _MP4FORMAT_H_

#include <mp4v2/mp4v2.h>

#ifdef __cplusplus

#include <medkit/mp4recorder.h>
#include <medkit/mp4player.h>

/**
 * Convert an asterisk format into a medkit codec type.
 * @return false if the format is not supported.
 */
bool AstFormatToCodec( int format, AudioCodec::Type &codec );
bool AstFormatToCodec( int format, VideoCodec::Type &codec );

/**
 * Asterisk binding for mp4recorder.
 * Adds the ability to record raw asterisk frames (ast_frame) on top of the
 * Asterisk-independent mp4recorder. All the actual MP4 writing is delegated to
 * the base class.
 */
class AstMp4Recorder : public mp4recorder
{
public:
    AstMp4Recorder( void * ctxdata, MP4FileHandle mp4, bool waitVideo )
        : mp4recorder( ctxdata, mp4, waitVideo ) {}

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

    /* Make the base (MediaFrame) overload visible through this class too */
    using mp4recorder::ProcessFrame;
};

#endif /* __cplusplus */

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

	void Mp4RecorderEnableVideoPrologue( struct mp4rec * r, bool yesno );

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

#endif /* _ASTFORMAT_H_ */
