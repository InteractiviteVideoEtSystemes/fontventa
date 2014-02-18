/* 
 * File:   framebuffer.h
 * Author: Emmanuel BUU
 *
 * Created on 24 de diciembre de 2012, 10:27
 */

#ifndef ASTFRAMEBUFFER_H
#define	ASTFRAMEBUFFER_H

#include <errno.h>
#include <asterisk/log.h>
#include <asterisk/frame.h>
#include <pthread.h>

#ifdef _cplusplus

#include <map>

class AstFrameBuffer 
{
public:
	AstFrameBuffer(bool blocking)
	{
		//NO wait time
		maxWaitTime = 0;
		//No hurring up
		hurryUp = false;
		//No canceled
		cancel = false;
		//No next
		next = (DWORD)-1;
		//Crete mutex
		pthread_mutex_init(&mutex,NULL);
		//Create condition
		pthread_cond_init(&cond,NULL);
		bigJumps = 0;
		
		this->blocking = blocking;
	}

	virtual ~AstFrameBuffer()
	{
		//Free packets
		Clear();
		//Destroy mutex
		pthread_mutex_destroy(&mutex);
	}

	bool Add(const ast_frame * f);

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

	struct ast_frame * Wait()
	{
		//NO packet
		struct ast_frame * rtp = NULL;

		//Get default wait time
		DWORD timeout = maxWaitTime;

		//Lock
		pthread_mutex_lock(&mutex);

		//While we have to wait
		while (!cancel)
		{
			//Check if we have somethin in queue
			if (!packets.empty())
			{

				//Get first
				RTPOrderedPackets::iterator it = packets.begin();
				//Get first seq num
				DWORD seq = it->first;
				//Get packet
				struct ast_frame * candidate = it->second;
				//Get time of the packet
				QWORD time = candidate->GetTime();

				//Check if first is the one expected or wait if not
				if (next==(DWORD)-1 || seq==next || time+maxWaitTime<getTime()/1000 || hurryUp)
				{
					//We have it!
					rtp = candidate;
					//Update next
					next = seq+1;
					//Remove it
					packets.erase(it);
					//Return it!
					break;
				}

				//We have to wait
				timespec ts;
				//Calculate until when we have to sleep
				ts.tv_sec  = (time_t) ((time+maxWaitTime) / 1e6) ;
				ts.tv_nsec = (long) ((time+maxWaitTime) - ts.tv_sec*1e6);
				
				//Wait with time out
				int ret = pthread_cond_timedwait(&cond,&mutex,&ts);
				//Check if there is an errot different than timeout
				if (ret && ret!=ETIMEDOUT)
					//Print error
					Error("-WaitQueue cond timedwait error [%d,%d]\n",ret,errno);
				
			} else {
				//Not hurryUp more
				hurryUp = false;
				//Wait until we have a new rtp pacekt
				int ret = pthread_cond_wait(&cond,&mutex);
				//Check error
				if (ret)
					//Print error
					Error("-WaitQueue cond timedwait error [%rd,%d]\n",ret,errno);
			}
		}
		
		//Unlock
		pthread_mutex_unlock(&mutex);

		//canceled
		return rtp;
	}

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
	DWORD			maxWaitTime;
	int			bigJumps;
	bool			blocking;
};

#endif

#ifdef _cplusplus
extern "C"
{
#endif
     struct AstFb;

     struct AstFb *AstFbCreate(struct AstFb *fb, DWORD maxWaitTime, int blocking);
     int AstFbAddFrame( struct AstFb *fb, struct ast_frame *f );
     struct ast_frame * AstFbGetFrame(struct AstFb *fb);
     DWORD AstFbLength(struct AstFb *fb);
     void AstFbCancel(struct AstFb *fb);     
     void AstFbReset(struct AstFb *fb);
     void AstFbDestroy(struct AstFb *fb);

#ifdef _cplusplus
}
#endif

#endif	/* RTPBUFFER_H */

