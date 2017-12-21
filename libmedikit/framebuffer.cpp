#include <medkit/log.h>
#include <astmedkit/framebuffer.h>

AstFrameBuffer::AstFrameBuffer(bool blocking, bool fifo)
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

	//Create condition
	bigJumps = 0;
	cycle = 0;
	
	this->blocking = blocking;
	this->isfifo = fifo;
	
	if ( pipe(this->pipe, O_NONBLOCK ) != 0 )
	{
		cancel = true;
	}
	signalled = false;
}

void AstFrameBuffer::Notify()
{
	if (blocking)
	{
		char c = 1;
		::write(this->pipe[0], &c, 1);
		signalled = true;
	}
}



bool AstFrameBuffer::Add(const ast_frame * f, bool ignore_cseq)
{
	DWORD seq;
	ast_frame * f2;
	
	std::lock_guard<std::mutex> guard(mutex);
	
	if (cancel) return false;
	
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
		

	//If already past
	if (next != (DWORD)-1 && seq < next && isfifo == 0)
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
			mutex.unlock();
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

	//Signal
	Notify();

	return true;
}

void  AstFrameBuffer::Cancel()
{
	//Lock
	mutex.lock();

	//Canceled
	cancel = true;

	//Unlock
	mutex.unlock();

	//Signal condition
	Notify();
}

void AstFrameBuffer::HurryUp()
{
	//Set flag
	mutex.lock();
	hurryUp = true;
	mutex.unlock();
	Notify();
}

struct ast_frame * AstFrameBuffer::Wait()
{
	//NO packet
	struct ast_frame * rtp = NULL;
	bool packready = false;
	//Get default wait time
	DWORD timeout = maxWaitTime;
	unsigned int len;

	//Lock
	std::unique_lock<std::mutex> lock(mutex);
	
	len = 0;
	//While we have to wait
	while (!cancel)
	{
		//Check if we have somethin in queue. In non blocking mode
		//we need three packets at least
		if ( ! packets.empty() )
		{
			RTPOrderedPackets::iterator it = packets.begin();
			//Get first seq num
			DWORD seq = it->first;
			//Get packet
			struct ast_frame * candidate = it->second;
			//Get time of the packet

			if (blocking)
			{
				packready = false;                             
			}
			else
			{
				packready = (packets.size() > maxWaitTime ); 
			}
			
			//Check if first is the one expected or wait if not
			if (next==(DWORD)-1 || seq==next || hurryUp || packready)
			{
				//We have it!
				rtp = it->second;
				if (seq==next) bigJumps = 0;
				
				//Update next
				next = seq+1;
				//Remove it
				packets.erase(it);
				//Return it!
				break;
			}
			
			if (seq < next)
			{
				packets.erase(it);
				continue;
			}
		} 
		
		if (blocking) 
		{
			char buff[4];
			
			mutex.unlock();
			ufds[0].fd = pipe[1];
			ufds[0].events = POLLIN | POLLERR | POLLHUP;
			int ret;
			
			if (maxWaitTime > 0)
			{
				struct pollfd ufds[1];
				
				ret = poll(ufds, 1, maxWaitTime);
				if (ret == 0)
				{
					// timeout
					next = (DWORD)-1;
				}
			}
			else
			{
				ret = poll(ufds, 1, -1);	
			}
			
			if (ret < 0)
			{
				break;
			}

			read(pipe[1], buff, 4);
			
		}
		else
		{
			break;
		}
	}
	
	//canceled
	return rtp;
}

void AstFrameBuffer::ClearPackets()
{
       //For each item, list shall be locked before
        for (RTPOrderedPackets::iterator it=packets.begin(); it!=packets.end(); ++it)
        {
                //Delete rtp
                ast_frfree(it->second);
        }

        //Clear all list
		packets.clear();
}

int AstFrameBuffer::FillFdTab(AstFrameBuffer * jbTab[], unsigned long nbjb, struct pollfd fds[], int idxMap[])
{
	if (nbjb > 0)
	{
		int nb = 0;
		for (unsigned long i=0; i<nbjb; i++)
		{
			if ( jbTab[i] == NULL ) return -3;
			
			if ( jbTab[i] != NULL && !jbTab[i]->cancel )
			{
				fds[nb].fd = jbTab[i]->pipe[1];
				fds[nb].events = POLLIN | POLLERR | POLLHUP;
				idxMap[nb] = i;
				nb++;
			}
			return nb;
		}
	}
	return nbjb;
}

#define MAX_FDS_FOR_JB 50

int AstFrameBuffer::WaitMulti(AstFrameBuffer * jbTab[], unsigned long nbjb, DWORD maxWaitTime, AstFrameBuffer * jbTabOut[])
{
	struct pollfd fds[MAX_FDS_FOR_JB];
	int idxMap[MAX_FDS_FOR_JB];
	int nb;
	
	if (nbjb > MAX_FDS_FOR_JB) nbjb = MAX_FDS_FOR_JB;
	if (nbjb > 0)
	{
		for (int i=0; i<nbjb; i++)
		{
			jbTabOut[i] = NULL;
		}
		
		nb = AstFrameBuffer::FillFdTab(jbTab, nbjb, fds, idxMap);
		if ( nb > 0 )
		{
			int ret = poll(fds, nb, maxWaitTime);
			if (ret > 0)
			{
				for (int i =0 ;i < nb; i++)
				{
					if (fds[i].revents & POLLIN)
					{
						 jbTabOut[idxMap[i]] = jbTab[idxMap[i]];
					}
					
					if (fds[i].revents & POLLERR)
					{
						return -20;
					}
				}
			}
			else if (ret == 0)
			{
				for (int i=0; i<nbjb; i++)
				{
					if (jbTab[i]) = jbTab[i]->HurryUp();
				}
			}
			else
			{
				return ret;
			}
		}
		return -4;
	}
	return -5;
}

/* ------------------------ C API ------------------------------------ */

struct AstFb *AstFbCreate(unsigned long maxWaitTime, int blocking, int fifo)
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

int AstFbWaitMulti(struct AstFb * fbTab[], unsigned long nbFb, unsigned long maxWaitTime, struct AstFb * fbTabOut[])
{
	fbTab2 = (AstFrameBuffer **) fbTab;
	fbTabOut2 = (AstFrameBuffer **) fbTabOut;
	return AstFrameBuffer::WaitMulti(fbTab2, nbFb, maxWaitTime, fbTabOut2);
}

