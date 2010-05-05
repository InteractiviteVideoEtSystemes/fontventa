#ifndef _H324M_H_
#define _H324M_H_

#define MEDIA_AUDIO 	0
#define MEDIA_VIDEO 	1

#define CODEC_AMR  	0
#define CODEC_H263	1 

#define CALLSTATE_NONE		0
#define CALLSTATE_SETUP		1
#define CALLSTATE_SETUPMEDIA	2
#define CALLSTATE_STABLISHED	3
#define CALLSTATE_HANGUP	4

#ifdef __cplusplus
extern "C"
{
#endif
void 	TIFFReverseBits(unsigned char* buffer,int length);
void	H324MSetReverseBits(int reverse);
void 	H324MLoggerSetLevel(int level);

void*	H324MSessionCreate(void);
void	H324MSessionDestroy(void * id);

int	H324MSessionInit(void * id);
int 	H324MSessionResetMediaQueue(void * id);
int	H324MSessionEnd(void * id);

int	H324MSessionRead(void * id,unsigned char *buffer,int len);
int	H324MSessionWrite(void * id,unsigned char *buffer,int len);

void*	H324MSessionGetFrame(void * id);
int	H324MSessionSendFrame(void * id,void *frame);

char* 	H324MSessionGetUserInput(void * id);
int  	H324MSessionSendUserInput(void * id,char *input);

int	H324MSessionSendVideoFastUpdatePicture(void * id);
int	H324MSessionGetState(void * id);

void* 	FrameCreate(int type,int codec, unsigned char * buffer, int len);
int 	FrameGetType(void* frame);
int 	FrameGetCodec(void* frame);
unsigned char * FrameGetData(void* frame);
unsigned int 	FrameGetLength(void *frame);
void 	FrameDestroy(void *frame);

#ifdef __cplusplus    
}
#endif

#endif
