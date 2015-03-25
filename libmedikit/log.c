#include <medkit/log.h>

#ifdef localtime_r
#undef localtime_r
#endif


FILE * logfile = NULL;
FILE * errfile = NULL;

int LogOpenFile(const char * filename)
{
    if (filename == NULL) filename = "/var/log/libmedkit.log" ;
    logfile = fopen(filename, "a+");
    if (logfile != NULL)
    {
	errfile = logfile;
        fputs("Log file open\n", logfile);
	return 1;
    }
    else
    {
        logfile = stdout;
	return 0;
    }
}

void LogCloseFile()
{
    if ( logfile != stdout )
    {
        fclose(logfile);
        logfile = stdout;
    }
    errfile = stderr;
}

static inline const char *LogFormatDateTime(char *buffer, size_t bufSize)
{
	struct timeval tv2;

	struct tm tm;
	char msstr[20];
	gettimeofday(&tv2,NULL);

	long ms = tv2.tv_usec/1000;
	sprintf( msstr, "%03ld", ms );
	localtime_r(&tv2.tv_sec, &tm);
	strftime( buffer, bufSize, "%Y-%m-%dT%H:%M:%S.", &tm );
	strcat(buffer, msstr); 
	return buffer;
}

static int LogToFile(const char *msg, va_list ap)
{
	char buf[80];
	
	if (logfile == NULL) return 0;
	fprintf(logfile, "[0x%lx][%s][LOG]", (long) pthread_self(),LogFormatDateTime(buf, sizeof(buf)));
	vfprintf(logfile, msg, ap);
	fflush(logfile);
	return 1;
}

static int DebugToFile(const char *msg, va_list ap)
{
	vprintf(msg, ap);
	return ;
}

static int ErrorToFile(const char *msg, va_list ap)
{
	struct timeval tv2;
	
	if (errfile == NULL) return 0;
	gettimeofday(&tv2,NULL);
	fprintf(errfile, "[0x%lx][%.10ld.%.3ld][ERR]", (long) pthread_self(),(long)tv2.tv_sec,(long)tv2.tv_usec/1000);
	vfprintf(errfile, msg, ap);
	return 0;
}

static logfunc FunctionDebug = DebugToFile;
static logfunc FunctionLog = LogToFile;
static logfunc FunctionError = ErrorToFile;

void SetLogFunctions(logfunc dbg, logfunc log, logfunc err)
{
	FunctionDebug = dbg;
	FunctionLog = log;
	FunctionError = err;
}

int Log(const char *msg, ...)
{
	va_list ap;
	FunctionLog(msg, ap);
	va_end(ap);
	return 0;
}

int Debug(const char *msg, ...)
{
	va_list ap;
	FunctionDebug(msg, ap);
	va_end(ap);
	return 0;	
}

int Error(const char *msg, ...)
{
	va_list ap;
	FunctionError(msg, ap);
	va_end(ap);
	return 0;
}