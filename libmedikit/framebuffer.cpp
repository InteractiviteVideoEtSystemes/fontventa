#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <medkit/log.h>
#include <astmedkit/framebuffer.h>

AstFrameBuffer::AstFrameBuffer(bool blocking, bool fifo)
{
	int flags;
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

	pthread_mutex_init(&mutex,NULL);
	//Create condition
	pthread_cond_init(&cond,NULL);	
	pcond = &cond;
	
	signalled = false;
	
	traceFile = NULL;
}

AstFrameBuffer::~AstFrameBuffer()
{
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
    Clear();
	if (traceFile) fclose(traceFile);
}

void AstFrameBuffer::Notify()
{
	pthread_cond_signal(pcond);
}


bool AstFrameBuffer::Add(const ast_frame * f, bool ignore_cseq)
{
	DWORD seq;
	ast_frame * f2;
	
	pthread_mutex_lock(&mutex);
	if (cancel) 
	{
		pthread_mutex_unlock(&mutex);
		return false;
	}
	
	if (ignore_cseq || isfifo)
	{
		seq = dummyCseq;
		dummyCseq++;
	}
	else
	{
		seq = f->seqno + cycle*0x10000;
		
		// Ignoring strange frames
		if (f->seqno > 0xFFFF)
		{
			ast_log(LOG_NOTICE, "Invalid seqno %d for frame.\n", f->seqno);
			pthread_mutex_unlock(&mutex);
			return false;
		}
		
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

		if (packets.find(seq) != packets.end())
		{
			//ast_log(LOG_DEBUG, "Received duplicate packet %ld.\n", seq);
			if (traceFile) fprintf(traceFile, "ADD: duplicate packet seq %lu\n", seq);
			pthread_mutex_unlock(&mutex);
			return false;
		}
	}
		
	//ast_log(LOG_DEBUG, "Adding packet %p seq=%ld, isfifo=%d, ignorecseq=%d.\n", this, seq, isfifo, ignore_cseq);

	//If already past
	if (next != (DWORD)-1 && seq < next && isfifo == 0)
	{
		//Delete pacekt
		//Skip it and lost forever
		if ( bigJumps > 20)
		{
			ast_log(LOG_WARNING, "Too many out of sequence packet. Resyncing.\n");
			if (traceFile) fprintf(traceFile,  "ADD: seq=%lu < next=%lu: too many out of sequence packet\n", seq, next);
			hurryUp  = true;
			bigJumps = 0;
			
		}
		else 
		{
			DWORD diff = next-seq;

			if (diff > 10)
			{
				ast_log(LOG_WARNING, "-Out of order non recoverable packet: %p seq=%u, next=%u diff=%u\n", this, seq, next, diff);
				bigJumps++;
			}
			if (traceFile) fprintf("ADD: seq=%lu < next=%lu: out of sequence packet. dropping it\n", seq, next);
			pthread_mutex_unlock(&mutex);
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

	pthread_mutex_unlock(&mutex);
	//Signal
	Notify();

	return true;
}

void  AstFrameBuffer::Cancel()
{
	//Lock
	pthread_mutex_lock(&mutex);

	//Canceled
	cancel = true;

	//Unlock
	pthread_mutex_unlock(&mutex);

	//Signal condition
	Notify();
}

void AstFrameBuffer::HurryUp()
{
	//Lock
	pthread_mutex_lock(&mutex);
	hurryUp = true;
	//Unlock
	pthread_mutex_unlock(&mutex);
	Notify();
}


static int conf_wait_timeout(pthread_cond_t * p_cond, pthread_mutex_t * p_mutex, long ms)
{
	timespec ts;
	struct timeval now;
		
	gettimeofday(&now,0);
	
	if (ms <= 0) return 0;
	//Calculate until when we have to sleep
	ts.tv_sec  = (time_t) (now.tv_sec + ms / 1000);
	now.tv_usec += (ms % 1000)*1000;
	if (now.tv_usec > 1000000)
	{
		ts.tv_sec++;
		now.tv_usec -= 1000000;
	}
	ts.tv_nsec = now.tv_usec*1000;
	
	return pthread_cond_timedwait(p_cond,p_mutex,&ts);
}

struct ast_frame * AstFrameBuffer::Wait(bool block)
{
	//NO packet
	struct ast_frame * rtp = NULL;
	//Get default wait time
	DWORD timeout = maxWaitTime;
	unsigned int len;
	char buff[4];

	//Lock
	pthread_mutex_lock(&mutex);
	
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
/*
			if (seq != next)
			    Log("seq=%lu, next=%lu, sz=%u, blocking=%d\n", seq, next, sz, blocking);
*/			
			//Check if first is the one expected or wait if not
			if (HasPacketReady(seq))
			{
				//We have it!
				rtp = it->second;
				nbLost = 0;

				if (seq==next) 
				{
					bigJumps = 0;
					if (traceFile) fprintf("GET: seq=%lu - normal case.\n", seq, next);
				}
				else if (next != (DWORD)-1 && seq > next)
				{
					nbLost = seq - next;
					if (traceFile) fprintf(traceFile, "GET: seq=%lu. lost packets: %lu\n", seq, nbLost);
				}
				else if (next == (DWORD)-1)
				{
					if (traceFile) fprintf(traceFile, "GET: seq=%lu next=0xFFFFFFFF\n", seq);
				}
				else if (hurryUp)
				{
					if (traceFile) fprintf(traceFile, "GET: seq=%lu HURRY UP\n", seq);
				}
				else
				{
					if (traceFile) fprintf(traceFile, "GET: seq=%lu strange case\n", seq);
				}
				
				//Update next
				next = seq+1;
				if (hurryUp) hurryUp = false;
				
			        //Log("Got packet buff=%p seq=%lu, next=%lu, blocking=%d\n", this, seq, next, blocking);
				//Remove it
				packets.erase(it);
				//Return it!
				break;
			}
			
			if (seq < next)
			{
				if (traceFile) fprintf(traceFile, "GET: dropping paquet seq=%lu, next=%lu\n", seq, next);
				packets.erase(it);
				continue;
			}
		} 
		
		if (blocking && block) 
		{			
			int ret;
			if (maxWaitTime > 0)
			{                        				
				ret = conf_wait_timeout(pcond,&mutex,maxWaitTime);
                //Check if there is an errot different than timeout
                		if (ret)
				{					
					if (ret != ETIMEDOUT)
                            Error("-WaitQueue cond timedwait error [%d,%d]\n",ret,errno);
					else
						hurryUp = true;
				}
			}
			else
			{
                ret = ETIMEDOUT;
				hurryUp = false;
				ret = pthread_cond_wait(pcond,&mutex);
			}
			
			if (ret < 0)
			{
				break;
			}		
		}
		else
		{
			// Non blocking
			break;
		}
	}
	
	pthread_mutex_unlock(&mutex);
	
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


#define MAX_FDS_FOR_JB 50

int AstFrameBuffer::WaitMulti(AstFrameBuffer * jbTab[], unsigned long nbjb, DWORD maxWaitTime, AstFrameBuffer * jbTabOut[])
{
	if (!jbTab[0]) return -1;
	
	if (nbjb > MAX_FDS_FOR_JB) nbjb = MAX_FDS_FOR_JB;
	if (nbjb > 0)
	{
		int ret;

		for (int i=0; i<nbjb; i++)
		{
			jbTabOut[i] = NULL;
		}
		
			/* Use condition pointer of first framebuffer. Normally, all pcond of all jitterbuffer should be the same */
			pthread_mutex_lock(&jbTab[0]->mutex);
			ret = conf_wait_timeout(jbTab[0]->pcond, &jbTab[0]->mutex, maxWaitTime);
			pthread_mutex_unlock(&jbTab[0]->mutex);
			if (ret >= 0)
			{
				ret = 0;
				for (int i =0 ;i < nbjb; i++)
				{
					if (jbTab[i])
					{
						pthread_mutex_lock(&jbTab[i]->mutex);
						if (! jbTab[i]->packets.empty() )
						{
							RTPOrderedPackets::iterator it = jbTab[i]->packets.begin();
							if ( jbTab[i]->HasPacketReady(it->first) )
							{
								jbTabOut[i] = jbTab[i];
								ret++;
							}
						}
						pthread_mutex_unlock(&jbTab[i]->mutex);
					}
				}
			}
			else if (ret == ETIMEDOUT)
			{
				for (int i=0; i<nbjb; i++)
				{
					if (jbTab[i] && jbTab[i]->Length() > 2) jbTab[i]->HurryUp();
				}
				ret = 0;
			}
			return ret;
	}
	return -5;
}

bool AstFrameBuffer::OpenTraceFile(const char * filename)
{
	if (traceFile != NULL)
	{
		traceFile = fopen(filename, "a");
		if (traceFile == NULL)
		{
			Error("Failed to open trace file %s. Error %s.\n", strerror(errno));
			return false;
		}
		return true;
	}
	Log("Trace file already open.\m");
	return true;
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
	return ((AstFrameBuffer *) fb)->Wait(false);
}

struct ast_frame * AstFbWaitFrame(struct AstFb *fb)
{
	return ((AstFrameBuffer *) fb)->Wait(true);
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

int AstFbGetLoss(struct AstFb *fb)
{
	return ((AstFrameBuffer *) fb)->GetLoss();
}


void AstFbUnblock(struct AstFb *fb)
{
	((AstFrameBuffer *) fb)->HurryUp();
}

void AstFbDestroy(struct AstFb *fb)
{
	AstFrameBuffer * fb2 = (AstFrameBuffer *) fb;
	if (fb2) delete fb2;
}

int AstFbWaitMulti(struct AstFb * fbTab[], unsigned long nbFb, unsigned long maxWaitTime, struct AstFb * fbTabOut[])
{
	AstFrameBuffer ** fbTab2 = (AstFrameBuffer **) fbTab;
	AstFrameBuffer ** fbTabOut2 = (AstFrameBuffer **) fbTabOut;
	return AstFrameBuffer::WaitMulti(fbTab2, nbFb, maxWaitTime, fbTabOut2);
}

void AstFbTrace(struct AstFb * fb, const char * filename)
{
	AstFrameBuffer * fb2 = (AstFrameBuffer *) fb;
	if (fb2)
	{
		fb2->OpenTraceFile(filename);
	}
}
