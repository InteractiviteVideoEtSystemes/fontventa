#include <astmedkit/astlog.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <medkit/log.h>

static int medkitdbglvl = 0;

static int LogToAst(const char *msg, va_list ap)
{
	char t[800];

	vsnprintf(t, 790, msg, ap);
	ast_log(LOG_NOTICE, "%s", t);
	return 1;
} 
	 
static int ErrorToAst(const char *msg, va_list ap)
{
	char t[800];

	vsnprintf(t, 790, msg, ap);
	ast_log(LOG_ERROR, "%s", t);
	return 1;
} 

static int DebugToAst(const char *msg, va_list ap)
{
	if ( option_debug >= medkitdbglvl)
	{
		char t[800];
		
		vsnprintf(t, 790, msg, ap);
		ast_log(LOG_DEBUG, "%s", t);
	}
	return 1;
} 

void RedirectLogToAsterisk(int libdbglevel)
{
	medkitdbglvl = libdbglevel;
	SetLogFunctions( DebugToAst, LogToAst, ErrorToAst );
}
