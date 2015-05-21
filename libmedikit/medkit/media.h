#ifndef _MEDIA_H_
#define	_MEDIA_H_
#include "config.h"
#include <stdlib.h>
#include <vector>
#include <string.h>

class MediaFrame
{
public:
	class Listener
	{
	public:
		virtual void onMediaFrame(MediaFrame &frame) = 0;
	};

	class RtpPacketization
	{
	public:
		RtpPacketization(DWORD pos,DWORD size,const BYTE* prefix,DWORD prefixLen, bool mark)
		{
			//Store values
			this->pos = pos;
			this->size = size;
			this->prefixLen = prefixLen;
			//Check size
			if (prefixLen)
				//Copy
				memcpy(this->prefix,prefix,prefixLen);
			this->mark = mark;
		}

		DWORD GetPos() const		{ return pos;	}
		DWORD GetSize()	const	{ return size;	}
		const BYTE* GetPrefixData() const { return prefix;	}
		DWORD GetPrefixLen()	{ return prefixLen;	}
		DWORD GetTotalLength()	{ return size+prefixLen;}
		bool IsMark() { return mark; }
		
		
	private:
		DWORD	pos;
		DWORD	size;
		BYTE	prefix[16];
		DWORD	prefixLen;
		bool mark;
	};

	typedef std::vector<RtpPacketization*> RtpPacketizationInfo;
public:
	enum Type {Audio=0,Video=1,Text=2,Application=3};
	enum MediaRole {VIDEO_MAIN=0,VIDEO_SLIDES=1 };
	
	static const char * TypeToString(Type type)
	{
		switch(type)
		{
			case Audio:
				return "Audio";
			case Video:
				return "Video";
			case Text:
				return "Text";
			case Application:
				return "Application";
			default:
				return "Unknown";
		}
	}

	MediaFrame(Type type,DWORD size, bool owns = true)
	{
		//Set media type
		this->type = type;
		//Set no timestamp
		ts = (DWORD)-1;
		//No duration
		duration = 0;
		//Set buffer size
		bufferSize = size;
		//Allocate memory
		
		//NO length
		length = 0;
		if ( owns )
		{
		    if ( size > 0 )
		    {
			buffer = (BYTE*) malloc(bufferSize);
		    }
		    else
		    {
			buffer = NULL;
		    }
		    ownsbuffer = true;
		}
		else
		{
		    buffer = NULL;
		    bufferSize = 0;
		    ownsbuffer = false;
		}
	}

	virtual ~MediaFrame()
	{
		//Clear
		ClearRTPPacketizationInfo();
		//Clear memory
		if (ownsbuffer) free(buffer);
	}

	void	ClearRTPPacketizationInfo()
	{
		//Emtpy
		while (!rtpInfo.empty())
		{
			//Delete
			delete(rtpInfo.back());
			//remove
			rtpInfo.pop_back();
		}
	}
	
	void	AddRtpPacket(DWORD pos,DWORD size,const BYTE* prefix,DWORD prefixLen, bool mark)
	{
		rtpInfo.push_back(new RtpPacketization(pos,size,prefix,prefixLen, mark));
	}
	
	Type	GetType() const		{ return type;	}
	DWORD	GetTimeStamp()	const	{ return ts;	}
	DWORD	SetTimestamp(DWORD ts)	{ this->ts = ts; }

	bool	HasRtpPacketizationInfo()		{ return !rtpInfo.empty();	}
	RtpPacketizationInfo& GetRtpPacketizationInfo()	{ return rtpInfo;		}
	virtual MediaFrame* Clone() = 0;

	DWORD GetDuration()	const 		{ return duration;		}
	void SetDuration(DWORD duration)	{ this->duration = duration;	}

	BYTE* GetData()	const		{ return buffer;	}
	DWORD GetLength() const		{ return length;		}
	DWORD GetMaxMediaLength() const	{ return bufferSize;		}

	void SetLength(DWORD length)	{ this->length = length;	}

	bool Alloc(DWORD size)
	{
		//Calculate new size
		bufferSize = size;
		//Realloc
		if ( ownsbuffer )
		{
		    buffer = (BYTE*) realloc(buffer,bufferSize);
		    if (length > bufferSize) length = bufferSize;
		}
		else
		{
			BYTE * nbuffer = (BYTE*) malloc(bufferSize);
			ownsbuffer = true;
			if (length <= bufferSize && buffer != NULL)
			{
				memcpy(nbuffer, buffer, length);
			}
			else
			{
				length = 0;
			}
			buffer = nbuffer;
		}
	}

	bool SetMedia(const BYTE* data,DWORD size)
	{
	    if ( ownsbuffer )
	    {
		//Check size
		if (size>bufferSize)
			//Allocate new size
			Alloc(size*3/2);
		//Copy
		memcpy(buffer,data,size);
		//Increase length
		length=size;
	    }
	    else
	    {
	        buffer = (BYTE*) data;
		length = size;
	    }
	    return true;
	}

	DWORD AppendMedia(const BYTE* data,DWORD size)
	{
		DWORD pos = length;
		
		if ( !ownsbuffer ) return 0;
		//Check size
		if (size+length>bufferSize)
			//Allocate new size
			Alloc((size+length)*3/2);
		//Copy
		memcpy(buffer+length,data,size);
		//Increase length
		length+=size;
		//Return previous pos
		return pos;
	}

	/*
	 * Create packetization info by parsing media
	 */
	virtual bool Packetize()
	{
		return false;
	}
	
protected:
	Type type;
	RtpPacketizationInfo rtpInfo;
	BYTE	*buffer;
	DWORD	length;
	DWORD	bufferSize;
	DWORD	duration;
	DWORD	clockRate;
	bool	ownsbuffer;
};

#endif	/* MEDIA_H */

