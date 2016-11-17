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
#include <pthread.h>
#include <map>

class AstFrameBuffer 
{
public:
	AstFrameBuffer(bool blocking, bool fifo)
	{
		//NO wait time
		maxWaitTime = 0;
		//No hurring up
		hurryUp = false;
		//No canceled
		cancel = false;
		//No next
		next = (DWORD)-1;
		dummyCseq = 0;
		//Crete mutex
		pthread_mutex_init(&mutex,NULL);
		//Create condition
		pthread_cond_init(&cond,NULL);
		bigJumps = 0;
		cycle = 0;
		
		this->blocking = blocking;
		this->isfifo = fifo;
	}

	virtual ~AstFrameBuffer()
	{
		//Free packets
		Clear();
		//Destroy mutex
		pthread_mutex_destroy(&mutex);
	}

	bool Add(const ast_frame * f, bool ignore_cseq = false);

	void Cancel()
	{
		//Lock
		pthread_mutex_lock(&mutex);

		//Canceled
		cancel = true;

		//Unlock
		pthread_mutex_unlock(&mutex);

		//Signal condition
		pthread_cond_signal(&cond);
	}

	struct ast_frame * Wait();

	void Clear()
	{
		//Lock
		pthread_mutex_lock(&mutex);

		//And remove all from queue
		ClearPackets();

		//UnLock
		pthread_mutex_unlock(&mutex);
	}

	void HurryUp()
	{
		//Set flag
		hurryUp = true;
		pthread_cond_signal(&cond);
	}

	void Reset(bool clear = true)
	{
		//Lock
		pthread_mutex_lock(&mutex);

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
		pthread_mutex_unlock(&mutex);
	}

	DWORD Length()
	{
		//REturn objets in queu
		return packets.size();
	}
	
	void SetMaxWaitTime(DWORD maxWaitTime)
	{
		this->maxWaitTime = maxWaitTime;
	}
private:
	void ClearPackets()
	{
		//For each item, list shall be locked before
		for (RTPOrderedPackets::iterator it=packets.begin(); it!=packets.end(); ++it)
			//Delete rtp
			delete(it->second);
		//Clear all list
		packets.clear();
	}

private:
	typedef std::map<DWORD,struct ast_frame *> RTPOrderedPackets;

private:
	//The event list
	RTPOrderedPackets	packets;
	bool			cancel;
	bool			hurryUp;
	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	DWORD			next;
	DWORD			dummyCseq;
	DWORD			cycle;
	DWORD			maxWaitTime;
	int				bigJumps;
	bool			blocking;
	bool			isfifo;
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

     struct ast_frame * AstFbGetFrame(struct AstFb *fb);
     uint32_t AstFbLength(struct AstFb *fb);
     void AstFbCancel(struct AstFb *fb);     
     void AstFbReset(struct AstFb *fb);
     void AstFbDestroy(struct AstFb *fb);

#ifdef __cplusplus
}
#endif

#endif	/* RTPBUFFER_H */

