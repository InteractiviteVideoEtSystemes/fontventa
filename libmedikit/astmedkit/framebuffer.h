/* 
 * File:   framebuffer.h
 * Author: Emmanuel BUU
 *
 * Created on 24 de diciembre de 2012, 10:27
 */

#ifndef ASTFRAMEBUFFER_H
#define	ASTFRAMEBUFFER_H

#include <errno.h>
#include <asterisk/frame.h>


#ifdef __cplusplus
#include <medkit/astcpp.h>
#include <medkit/config.h>
#include <mutex>
#include <map>

class AstFrameBuffer 
{
public:
	AstFrameBuffer(bool blocking, bool fifo);

	virtual ~AstFrameBuffer();

	bool Add(const ast_frame * f, bool ignore_cseq = false);

	void Cancel();

	struct ast_frame * Wait(bool block);

	void Clear()
	{
		//Lock
		mutex.lock();

		//And remove all from queue
		ClearPackets();

		//UnLock
		mutex.unlock();
	}

	void HurryUp();

	void Reset(bool clear = true)
	{
		//Lock
		mutex.lock();

		//And remove cancel
		cancel = false;
		
		//And remove all from queue
		if (clear) 
		{
			ClearPackets();
			//No next
			next = (DWORD)-1;
		}
		
		bigJumps = 0;
		
		//UnLock
		mutex.unlock();
	}

	DWORD Length()
	{
		mutex.lock();
		DWORD l = packets.size();
		mutex.unlock();
		//REturn objets in queu
		return  l;
	}
	
	int GetLoss() { return nbLost; }
	
	void SetMaxWaitTime(DWORD maxWaitTime)
	{
		this->maxWaitTime = maxWaitTime;
	}
	
	static int FillFdTab(AstFrameBuffer * jbTab[], unsigned long nbjb, struct pollfd fds[], int idxMap[]); 
	static int WaitMulti(AstFrameBuffer * jbTab[], unsigned long nbFb, DWORD maxWaitTime, AstFrameBuffer * jbTabOut[]);

private:
	void ClearPackets();
	void Notify();

private:
	typedef std::map<DWORD,struct ast_frame *> RTPOrderedPackets;

private:
	//The event list
	RTPOrderedPackets	packets;
	bool			cancel;
	bool			signalled;
	bool			hurryUp;
	std::mutex		mutex;
	DWORD			next;
	DWORD			dummyCseq;
	DWORD			cycle;
	DWORD			maxWaitTime;
	DWORD			prevTs;
	int				bigJumps;
	bool			blocking;
	bool			isfifo;
	int				pipe[2];
	int				nbLost;
};

#endif

#ifdef __cplusplus
extern "C"
{
#endif
     struct AstFb;

     struct AstFb *AstFbCreate(unsigned long maxWaitTime, int blocking, int fifo);
     /**
      *  Add an ast_frame into the jitterbuffer. Frame is duplicated.
      *  
      *  @param fb jitterbuffer instance to consider
      *  @param[in]  f frame to post. f->ts must be correctly set.       
      */
     int AstFbAddFrame( struct AstFb *fb, const struct ast_frame *f );     
     int AstFbAddFrameNoCseq( struct AstFb *fb, const struct ast_frame *f );

	void AstFbUnblock(struct AstFb *fb);
	 
	// Always non blocking
    struct ast_frame * AstFbGetFrame(struct AstFb *fb);
	 
	//Blocking if Jb is created blocking otherwise, behave as AstFbGetFrame()
	struct ast_frame * AstFbWaitFrame(struct AstFb *fb);
	
	// if frame is returned, get number of packet lost
	int AstFbGetLoss(struct AstFb *fb);
	
     uint32_t AstFbLength(struct AstFb *fb);
     void AstFbCancel(struct AstFb *fb);     
     void AstFbReset(struct AstFb *fb);
     void AstFbDestroy(struct AstFb *fb);

	int AstFbWaitMulti(struct AstFb * fbTab[], unsigned long nbFb, unsigned long maxWaitTime, struct AstFb * fbTabOut[]);
	 
#ifdef __cplusplus
}
#endif

#endif	/* RTPBUFFER_H */

