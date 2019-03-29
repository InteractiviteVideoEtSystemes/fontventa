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
		pthread_mutex_lock(&mutex);

		//And remove all from queue
		ClearPackets();

		//UnLock
		pthread_mutex_unlock(&mutex);
	}

	void HurryUp();

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
		pthread_mutex_lock(&mutex);
		DWORD l = packets.size();
		pthread_mutex_unlock(&mutex);
		//REturn objets in queu
		return  l;
	}
	
	int GetLoss() { return nbLost; }
	
	void SetMaxWaitTime(DWORD maxWaitTime)
	{
		this->maxWaitTime = maxWaitTime;
	}
	
	void SetCondVariable(pthread_cond_t * pcond)
	{
		if (pcond) 
			this->pcond = pcond;
		else
			this->pcond = &cond;
	}
		
	static int WaitMulti(AstFrameBuffer * jbTab[], unsigned long nbFb, DWORD maxWaitTime, AstFrameBuffer * jbTabOut[]);

	bool OpenTraceFile(const char * filename);
	
private:
	void ClearPackets();
	void Notify();

	inline bool HasPacketReady(DWORD seq)
	{
		bool packready;
		unsigned int sz = 0;

		if (next==(DWORD)-1 || seq==next || hurryUp) return true;
		
		sz = packets.size();
		if (blocking)
		{
			packready = (sz > (maxWaitTime/20) ); 
		}
		else
		{
			//packready = (packets.size() > maxWaitTime ); 
			packready = (sz > maxWaitTime ); 
		}

		return packready;
	}

private:
	typedef std::map<DWORD,struct ast_frame *> RTPOrderedPackets;

private:
	//The event list
	RTPOrderedPackets	packets;
	bool			cancel;
	bool			signalled;
	bool			hurryUp;
	DWORD			next;
	DWORD			dummyCseq;
	DWORD			cycle;
	DWORD			maxWaitTime;
	DWORD			prevTs;
	int				bigJumps;
	bool			blocking;
	bool			isfifo;
	
	pthread_mutex_t         mutex;
	pthread_cond_t *        pcond;
	pthread_cond_t          cond;

	int				nbLost;
	FILE *			traceFile;
};

#endif

#ifdef __cplusplus
extern "C"
{
#endif
     struct AstFb;

	 
	/**
      *  Create an instance of jitterbuffer.
      *  
      *  @param [in] if jb is created in blocking mode, maxWaitTime time expresses the max duration
	  *             after which the reader is unblocked. In this case, the first packet available is returned
	  *             event if its sequence number is not the one expected.
	  *
	  *				if jb is created is non-blocking mode, maxWaitTime expresses the number of packet above which
	  *				jb returns a packet event if some packet loss occurred 
	  *
      *  @param [in] blocking: if true, the AstFbWaitFrame() function will block (blocking mode).
	  *
	  *  @param [in] fifo: if true, sequence number of packets are not considered and jb behaves like a fifo packet queue.
     **/
     struct AstFb *AstFbCreate(unsigned long maxWaitTime, int blocking, int fifo);
	
     /**
      *  Add an ast_frame into the jitterbuffer. Frame is duplicated.
      *  
      *  @param fb: jitterbuffer instance to consider
      *  @param [in] f: frame to post. f->ts must be correctly set.       
     **/
     int AstFbAddFrame( struct AstFb *fb, const struct ast_frame *f ); 

     /**
      *  Add an ast_frame into the jitterbuffer but ignore sequence number. Frame is duplicated.
      *  
      *  @param fb: jitterbuffer instance to consider
      *  @param [in] f: frame to post.       
     **/
	 
     int AstFbAddFrameNoCseq( struct AstFb *fb, const struct ast_frame *f );

	void AstFbUnblock(struct AstFb *fb);
	 
	// Always non blocking
    struct ast_frame * AstFbGetFrame(struct AstFb *fb);
	 
	//Blocking if Jb is created blocking otherwise, behave as AstFbGetFrame()
	struct ast_frame * AstFbWaitFrame(struct AstFb *fb);
	
	// if frame is returned, get number of packet lost
	int AstFbGetLoss(struct AstFb *fb);
	
	uint32_t AstFbLength(struct AstFb *fb);
	
	
	/**
     *  Cancel .
     *  
     *  @param fb jitterbuffer instance to reset
     **/

	void AstFbCancel(struct AstFb *fb);

    /**
     *  Clear all packets of a frame buffer.
     *  
     *  @param fb jitterbuffer instance to reset
     **/
 	
	void AstFbReset(struct AstFb *fb);
	
	/**
     *  Destroy an instance of jitter buffer
     *  
     *  @param fb jitterbuffer instance to destroy
     **/ 
	void AstFbDestroy(struct AstFb *fb);

     /**
      *  this function is used when serveral jitterbuffers need to be read by a single thread.
	  *  it replace the internal condition variable of those jb by a single shared condition
	  *  variable provided by the caller.
      *  
      *  @param fbTab: array of jitterbuffers to modify
      *  @param nbFb of jitterbuffers in the array. 
      *  @param maxWaitTime: maximimum time to wait before returns even if no packet is available
      *  @param fbTabOut: list of JB ready to be read. Has the same size than input array but some
      *                   element may be NULL indicating that the JB has no packet ready to be read.
     **/
	int AstFbWaitMulti(struct AstFb * fbTab[], unsigned long nbFb, unsigned long maxWaitTime, struct AstFb * fbTabOut[]);
	
    /**
      *  this function is used when serveral jitterbuffers need to be read by a single thread.
	  *  it is similar to select() or poll() for file descriptors. It wait for packets on set of jb
	  *  if at least one jb has a packet ready, it unblocks and returns the array of jb ready 
	  *  to be read. The reader will then need to call AstFbGetFrame() on each jb that are 
	  *  fbTabOut. In order to work properly, the calle need to create an external pthread_cond_t
	  *  with pthread_cond_init(() then pass it to the set of jb to read by calling AstFbCondMulti().
	  *
	  *
      *  
      *  @param fbTab: array of jitterbuffers to modify
      *  @param nbFb: of jitterbuffers in the array. 
	  *  @param maxWaitTime: maximum time to wait
	  *  @param [out] fbTabOut: set of jb ready to be read returned by this function. The reader need to
	  *                         iterate over the fbTabOut up to index nbFb. All non null slots are frame buffers
	  *                         ready to be read. 
     **/

	int AstFbWaitMulti(struct AstFb * fbTab[], unsigned long nbFb, unsigned long maxWaitTime, struct AstFb * fbTabOut[]);
	
	
	void AstFbTrace(struct AstFb * fb, const char * filename);
	
#ifdef __cplusplus
}
#endif

#endif	/* RTPBUFFER_H */

