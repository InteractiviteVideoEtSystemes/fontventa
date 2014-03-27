#include <medkit/audio.h>

/* 
 * Create a new frame with silence sound inside
 * will return NULL if codec is not supported.
 * Frame must be freed after being used
 */
AudioFrame * GetSilenceFrame( AudioCodec::Type codec );
