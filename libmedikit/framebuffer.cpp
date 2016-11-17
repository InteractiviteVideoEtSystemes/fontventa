#include <medkit/log.h>
#include <astmedkit/framebuffer.h>


bool AstFrameBuffer::Add(const ast_frame * f, bool ignore_cseq)
{
	DWORD seq;
	ast_frame * f2;
		    
	if (ignore_cseq || isfifo)
	{
		seq = dummyCseq;
		dummyCseq++;
	}
	else
	{
		seq = f->seqno + cycle*0x10000;
		if (f->seqno == 0xFFFF)
		{
			cycle++;
		}
		else if (seq < dummyCseq && dummyCseq-seq > 0xF000 )
		{
			// We missed the cycle because of packet loss ... 
			cycle++;
			// We need to reecompute seq
			seq = f->seqno + cycle*0x10000;
		}
		dummyCseq = seq + 1;
	}
		
	
	//Lock
	pthread_mutex_lock(&mutex);

	//If already past
	if (next != (DWORD)-1 && seq < next)
	{
		bigJumps++;
		//Delete pacekt
		//Skip it and lost forever
		if ( bigJumps > 20)
		{
			ast_log(LOG_WARNING, "Too many out of sequence packet. Resyncing.\n");
			next=(DWORD)-1;
			bigJumps = 0;
			
		}
		else 
		{
			//Delete pacekt
			//ast_frfree(f);
			//Unlock
			pthread_mutex_unlock(&mutex);
			ast_log(LOG_WARNING, "-Out of order non recoverable packet: seq=%ld, next=%ld diff=%ld\n",
					seq, next, next-seq);
			return false;
		}
	}

	//Add event
	f2 = ast_frdup(f);
	
	// 0 = use native CSEQ
	// 1 = overwrite CSEQ
	// 2 = behave as simple fifo but do NOT rewrite CSEQ

	if (ignore_cseq) f2->seqno = (seq & 0xFFFF);
	packets[seq] = f2;

	//Unlock
	pthread_mutex_unlock(&mutex);

	//Signal
	pthread_cond_signal(&cond);

	return true;
}

struct ast_frame * AstFrameBuffer::Wait()
{
	//NO packet
	struct ast_frame * rtp = NULL;
	bool packready = false;
	//Get default wait time
	DWORD timeout = maxWaitTime;

	//Lock
	pthread_mutex_lock(&mutex);

	//While we have to wait
	while (!cancel)
	{
		//Check if we have somethin in queue. In non blocking mode
		//we need three packets at least
		if ( ! packets.empty() )
		{
			int ret = ETIMEDOUT;
			//Get first
			RTPOrderedPackets::iterator it = packets.begin();
			//Get first seq num
			DWORD seq = it->first;
			//Get packet
			struct ast_frame * candidate = it->second;
			//Get time of the packet
			
			QWORD time = candidate->ts;
			
			if (blocking)
			{
				packready = ( time+maxWaitTime<getTime()/1000 );
			}
			else
			{
				packready = ( seq <= next ) || Length() > maxWaitTime ) 
			}

			//Check if first is the one expected or wait if not
			if (next==(DWORD)-1 || seq==next || packready || hurryUp)
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
			if (blocking) ret = pthread_cond_timedwait(&cond,&mutex,&ts);
			//Check if there is an errot different than timeout
			if (ret && ret!=ETIMEDOUT)
				//Print error
				Error("-WaitQueue cond timedwait error [%d,%d]\n",ret,errno);
			
		} 
		else 
		{
			int ret = ETIMEDOUT;
			//Not hurryUp more
			hurryUp = false;
			//Wait until we have a new rtp pacekt
			if (blocking) 
				ret = pthread_cond_wait(&cond,&mutex);
			else
				break;
				
			//Check error
			if (ret && ret!=ETIMEDOUT)
			{
				//Print error
				Error("-WaitQueue cond timedwait error [%rd,%d]\n",ret,errno);
				break;
			}
		}
	}
	
	//Unlock
	pthread_mutex_unlock(&mutex);

	//canceled
	return rtp;
}

struct AstFb *AstFbCreate(DWORD maxWaitTime, int blocking, int fifo)
{
	AstFrameBuffer * fb = new AstFrameBuffer((bool) blocking, (bool) fifo);
	if (fb)
	{
		fb->SetMaxWaitTime(maxWaitTime);
	}
	return (struct AstFb *) fb;
}

int AstFbAddFrame( struct AstFb *fb, const struct ast_frame *f )
{
	return ((AstFrameBuffer *) fb)->Add( f, false );
}

int AstFbAddFrameNoCseq( struct AstFb *fb, const struct ast_frame *f )
{
	return ((AstFrameBuffer *) fb)->Add( f, true );
}

struct ast_frame * AstFbGetFrame(struct AstFb *fb)
{
	return ((AstFrameBuffer *) fb)->Wait();
}

void AstFbCancel(struct AstFb *fb)
{
	((AstFrameBuffer *) fb)->Cancel();
}

void AstFbReset(struct AstFb *fb)
{
	((AstFrameBuffer *) fb)->Reset();
} 
 
DWORD AstFbLength(struct AstFb *fb)
{
	return ((AstFrameBuffer *) fb)->Length();
}
void AstFbDestroy(struct AstFb *fb)
{
	AstFrameBuffer * fb2 = (AstFrameBuffer *) fb;
	if (fb2) delete fb2;
}
