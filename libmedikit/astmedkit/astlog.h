#include <asterisk/log.h>


/**
 * Link libmedkit log facility with Asterik logs
 * @param libdbglevel debug level assigned to lib debug traces
 *        asterik will only printout debug traces of libmedkit
 *        if debug level set by "core set debug x" is higher and
 *        equals than the one passed to this func.
 */
void RedirectLogToAsterisk(int libdbglevel);
