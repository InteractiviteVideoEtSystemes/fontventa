#include <asterisk.h>
#include <video.h>

struct VideoTranscoder;
struct TextTranscoder;


typedef void (*VideoTranscoderCb)(void * ctxdata, int outputcodec, const char *output, size_t outputlen);

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
 * Obtain inbound codec, width and height of decoded picture
 * @param vtc: video transcoder instance
 * @param codec: source codec
 * @param width: width of decoded picture
 * @height height: height of decoded picture.
 * @return 0 : params are not available, 1 valid params
 */

int VideoTranscoderGetDecodedPicParams( struct VideoTranscoder *vtc, int * codec, DWORD * width, DWORD *height );