#include <asterisk.h>
#include <video.h>

struct VideoTranscoder;
struct TextTranscoder;


//typedef (*VideoTranscoderCb)(void * ctxdata, int outputcodec, const char *output, size_t outputlen);

/**
 * Create a video transcoder
 *
 * @param ctxdata: context data to be passed to callbacks
 * @param format: description of expected encoder output
 * @param cb: callback function to return each frame.
 * @return a new video transcoder instance or NULL if it fails
 */
struct VideoTranscoder * VideoTranscoderCreate(void * ctxdata,char *format, VideoTranscoderCb cb);

/**
 * Destroy a video transcoder
 *
 * @param vtc: video transcoder to destory
 * @return 1 if transcoder was destroyed correctly, 0 otherwise
 */
int VideoTranscoderDestroy(struct VideoTranscoder *vtc);

/**
 * Process one ast_frame
 *
 * @param vtc: video transcoder context
 * @param f:    video frame to process
 * @return 	0 frame was processed,
 *		1 picture is complete
 *		-1 could not decode
 *		-2
 *
 */
int VideoTranscoderProcessFrame(struct VideoTranscoder *vtc, const ast_frame * f);

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

 int Mp4RecordFrame