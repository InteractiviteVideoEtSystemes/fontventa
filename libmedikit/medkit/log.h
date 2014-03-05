#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

extern FILE * logfile;
extern FILE * errfile;


#ifdef __cplusplus
#include "tools.h"
extern "C"
{
#endif
int LogOpenFile(const char * filename);
void LogCloseFile();

#ifdef __cplusplus
}
#endif
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

static inline int Log(const char *msg, ...)
{
	char buf[80];
	
	if (logfile == NULL) return 0;
	va_list ap;
	fprintf(logfile, "[0x%lx][%s][LOG]", (long) pthread_self(),LogFormatDateTime(buf, sizeof(buf)));
	va_start(ap, msg);
	vfprintf(logfile, msg, ap);
	va_end(ap);
	fflush(logfile);
	return 1;
}

static inline int Log2(const char* prefix,const char *msg, ...)
{
	struct timeval tv2;
	va_list ap;
	
	if (logfile == NULL) return 0;
	gettimeofday(&tv2,NULL);
	fprintf(logfile, "[0x%lx][%.10ld.%.3ld][LOG]%s ", (long) pthread_self(),(long)tv2.tv_sec,(long)tv2.tv_usec/1000,prefix);
	va_start(ap, msg);
	vfprintf(logfile, msg, ap);
	va_end(ap);
	fflush(logfile);
	return 1;
}

static inline void Debug(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
	return ;
}

static inline int Error(const char *msg, ...)
{
	struct timeval tv;
	va_list ap;
	
	if (errfile == NULL) return 0;
	gettimeofday(&tv,NULL);
	fprintf(errfile, "[0x%lx][%.10ld.%.3ld][ERR]", (long) pthread_self(),(long)tv.tv_sec,(long)tv.tv_usec/1000);
	va_start(ap, msg);
	vfprintf(errfile, msg, ap);
	va_end(ap);
	return 0;
}

static inline char PC(uint8_t b)
{
        if (b>32&&b<128)
                return b;
        else
                return '.';
}


inline uint32_t BitPrint(char* out,uint8_t val,uint8_t n)
{
        int i, j=0;

        for (i=0;i<(8-n);i++)
                out[j++] = 'x';
        for (i=(8-n);i<8;i++)
                if ((val>>(7-i)) & 1)
                        out[j++] = '1';
                else
                        out[j++] = '0';
        out[j++] = ' ';
        out[j] = 0;

        return j;
}

static inline void BitDump(uint32_t val,uint8_t n)
{
	char line1[136];
	char line2[136];
	int i=0;
	if (n>24)
	{
		sprintf(line1,"0x%.2x     0x%.2x     0x%.2x     0x%.2x     ",(uint8_t)(val>>24),(uint8_t)(val>>16),(uint8_t)(val>>8),(uint8_t)(val));
		i+= BitPrint(line2,(uint8_t)(val>>24),n-24);
		i+= BitPrint(line2+i,(uint8_t)(val>>16),8);
		i+= BitPrint(line2+i,(uint8_t)(val>>8),8);
		i+= BitPrint(line2+i,(uint8_t)(val),8);
	} else if (n>16) {
		sprintf(line1,"0x%.2x     0x%.2x     0x%.2x     ",(uint8_t)(val>>16),(uint8_t)(val>>8),(uint8_t)(val));
		i+= BitPrint(line2+i,(uint8_t)(val>>16),n-16);
		i+= BitPrint(line2+i,(uint8_t)(val>>8),8);
		i+= BitPrint(line2+i,(uint8_t)(val),8);
	} else if (n>8) {
		sprintf(line1,"0x%.2x     0x%.2x     ",(uint8_t)(val>>8),(uint8_t)(val));
		i+= BitPrint(line2,(uint8_t)(val>>8),n-8);
		i+= BitPrint(line2+i,(uint8_t)(val),8);
	} else {
		sprintf(line1,"0x%.2x     ",(uint8_t)(val));
		BitPrint(line2,(uint8_t)(val),n);
	}
	Debug("Dumping 0x%.4x:%d\n\t%s\n\t%s\n",val,n,line1,line2);
}

#ifdef __cplusplus
inline void BitDump(WORD val)
{
	BitDump(val,16);
}

inline void BitDump(DWORD val)
{
	BitDump(val,32);
}

inline void BitDump(QWORD val)
{
	BitDump(val>>32,32);
	BitDump(val,32);
}
#endif

static inline void Dump(uint8_t *data,uint32_t size)
{
	int i;

	for(i=0;i<(size/8);i++)
		Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   %c%c%c%c%c%c%c%c]\n",8*i,data[8*i],data[8*i+1],data[8*i+2],data[8*i+3],data[8*i+4],data[8*i+5],data[8*i+6],data[8*i+7],PC(data[8*i]),PC(data[8*i+1]),PC(data[8*i+2]),PC(data[8*i+3]),PC(data[8*i+4]),PC(data[8*i+5]),PC(data[8*i+6]),PC(data[8*i+7]));
	switch(size%8)
	{
		case 1:
			Debug("[%.4x] [0x%.2x                                                    %c       ]\n",size-1,data[size-1],PC(data[size-1]));
			break;
		case 2:
			Debug("[%.4x] [0x%.2x   0x%.2x                                             %c%c      ]\n",size-2,data[size-2],data[size-1],PC(data[size-2]),PC(data[size-1]));
			break;
		case 3:
			Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x                                      %c%c%c     ]\n",size-3,data[size-3],data[size-2],data[size-1],PC(data[size-3]),PC(data[size-2]),PC(data[size-1]));
			break;
		case 4:
			Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x   0x%.2x                               %c%c%c%c    ]\n",size-4,data[size-4],data[size-3],data[size-2],data[size-1],PC(data[size-4]),PC(data[size-3]),PC(data[size-2]),PC(data[size-1]));
			break;
		case 5:
			Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x                        %c%c%c%c%c   ]\n",size-5,data[size-5],data[size-4],data[size-3],data[size-2],data[size-1],PC(data[size-5]),PC(data[size-4]),PC(data[size-3]),PC(data[size-2]),PC(data[size-1]));
			break;
		case 6:
			Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x                 %c%c%c%c%c%c  ]\n",size-6,data[size-6],data[size-5],data[size-4],data[size-3],data[size-2],data[size-1],PC(data[size-6]),PC(data[size-5]),PC(data[size-4]),PC(data[size-3]),PC(data[size-2]),PC(data[size-1]));
			break;
		case 7:
			Debug("[%.4x] [0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x   0x%.2x          %c%c%c%c%c%c%c ]\n",size-7,data[size-7],data[size-6],data[size-5],data[size-4],data[size-3],data[size-2],data[size-1],PC(data[size-7]),PC(data[size-6]),PC(data[size-5]),PC(data[size-4]),PC(data[size-3]),PC(data[size-2]),PC(data[size-1]));
			break;
	}
}

#ifdef __cplusplus
class Logger
{
public:
	class Listener
	{

	};

public:
        static Logger& getInstance()
        {
            static Logger   instance;
            return instance;
        }

	inline void Log(const char *msg, ...)
	{

	}
	
	inline int Error(const char *msg, ...)
	{
		return 0;
	}

private:
        Logger();
        // Dont forget to declare these two. You want to make sure they
        // are unaccessable otherwise you may accidently get copies of
        // your singelton appearing.
        Logger(Logger const&);			// Don't Implement
        void operator=(Logger const&);		// Don't implement
};

#endif

#endif
