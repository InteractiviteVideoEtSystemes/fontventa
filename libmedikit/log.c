#include <medkit/log.h>

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