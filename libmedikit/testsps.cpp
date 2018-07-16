#include <stdio.h>
#include <stdarg.h>

#include <medkit/log.h>
#include <medkit/bitstream.h>
#include <vector>
#include <h264/h264.h>

static int LogToAst(const char *msg, va_list ap)
{
        vprintf(msg, ap);
        return 1;
}

static int ErrorToAst(const char *msg, va_list ap)
{
        vprintf(msg, ap);
        return 1;
}

static int DebugToAst(const char *msg, va_list ap)
{
        vprintf(msg, ap);
        return 1;
}

int main(int argc, char *argv[])
{
	H264SeqParameterSet sps;

	BYTE spsBytesIos[] = { 0x42, 0x00, 0x1e, 0xab, 0x40, 0x50, 0x1e, 0xc8 };	


	SetLogFunctions( DebugToAst, LogToAst, ErrorToAst );
	sps.Decode(spsBytesIos, sizeof(spsBytesIos) );
	sps.Dump();
	Log("toto\n");
}
