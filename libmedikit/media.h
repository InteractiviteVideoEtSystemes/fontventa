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
		RtpPacketization(DWORD pos,DWORD size,BYTE* prefix,DWORD prefixLen, bool mark = false)
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

		DWORD GetPos()		{ return pos;	}
		DWORD GetSize()		{ return size;	}
		BYTE* GetPrefixData()	{ return prefix;	}
		DWORD GetPrefixLen()	{ return prefixLen;	}
		DWORD GetTotalLength()	{ return size+prefixLen;}
		bool  IsMark()		{ return mark; }
		
	private:
		DWORD	pos;
		DWORD	size;
		BYTE	prefix[16];
		DWORD	prefixLen;
		bool	mark;
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

	MediaFrame(Type type,DWORD size, bool ownsBuffer = true)
	{
		//Set media type
		this->type = type;
		//Set no timestamp
		ts = (DWORD)-1;
		//No duration
		duration = 0;
		
		this->ownsBuffer = ownsBuffer;
		
		if ( ownsBuffer )
		{
			//Set buffer size
			bufferSize = size;
			//Allocate memory
			buffer = (BYTE*) malloc(bufferSize);
		}
		else
		{
			buffer = NULL;
			bufferSize = 0;
		}
		//NO length
		length = 0;
	}

	virtual ~MediaFrame()
	{
		//Clear
		ClearRTPPacketizationInfo();
		//Clear memory
		if (ownsBuffer) free(buffer);
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
	
	void	AddRtpPacket(DWORD pos,DWORD size,BYTE* prefix,DWORD prefixLen, bool mark = false)		
	{
		rtpInfo.push_back(new RtpPacketization(pos,size,prefix,prefixLen, mark));
	}
	
	Type	GetType()		{ return type;	}
	DWORD	GetTimeStamp()		{ return ts;	}
	DWORD	SetTimestamp(DWORD ts)	{ this->ts = ts; }

	bool	HasRtpPacketizationInfo()		{ return !rtpInfo.empty();	}
	RtpPacketizationInfo& GetRtpPacketizationInfo()	{ return rtpInfo;		}
	virtual MediaFrame* Clone() = 0;

	DWORD GetDuration()			{ return duration;		}
	void SetDuration(DWORD duration)	{ this->duration = duration;	}

	BYTE* GetData()			{ return buffer;		}
	DWORD GetLength()		{ return length;		}
	DWORD GetMaxMediaLength()	{ return bufferSize;		}

	void SetLength(DWORD length)	{ this->length = length;	}

	bool Alloc(DWORD size)
	{
		//Calculate new size
		bufferSize = size;
		//Realloc
		buffer = (BYTE*) realloc(buffer,bufferSize);
	}

	bool SetMedia(BYTE* data,DWORD size)
	{
	    if (ownsBuffer)
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
	        length=size;
		buffer=data;
	    }
	}

	DWORD AppendMedia(BYTE* data,DWORD size)
	{
	    if ( ownsBuffer )
	    {
		DWORD pos = length;
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
	    else
	    {
	        return 0;
	    }
	}
	
protected:
	Type type;
	DWORD ts;
	RtpPacketizationInfo rtpInfo;
	BYTE	*buffer;
	DWORD	length;
	DWORD	bufferSize;
	DWORD	duration;
	DWORD	clockRate;
	bool ownsBuffer;
};

#endif	/* MEDIA_H */

