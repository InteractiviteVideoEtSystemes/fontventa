#ifndef _AUDIO_H_
#define _AUDIO_H_
#include "config.h"
#include "media.h"
#include "codecs.h"

class AudioEncoder
{
public:
	virtual int   Encode(SWORD *in,int inLen,BYTE* out,int outLen)=0;
	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	virtual DWORD GetClockRate()=0;
	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;
};

class AudioDecoder
{
public:
	virtual int   Decode(BYTE *in,int inLen,SWORD* out,int outLen)=0;
	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;
};

class AudioFrame : public MediaFrame
{
public:
	AudioFrame(AudioCodec::Type codec,DWORD rate) : MediaFrame(MediaFrame::Audio,2048)
	{
		//Store codec
		this->codec = codec;
		//Set default rate
		this->rate = rate;
	}

	virtual MediaFrame* Clone()
	{
		//Create new one
		AudioFrame *frame = new AudioFrame(codec,rate);
		//Copy content
		frame->SetMedia(buffer,length);
		//Duration
		frame->SetDuration(duration);
		//Set timestamp
		frame->SetTimestamp(GetTimeStamp());
		//Return it
		return (MediaFrame*)frame;
	}

	AudioCodec::Type GetCodec()			{ return codec;		}
	void	SetCodec(AudioCodec::Type codec)	{ this->codec = codec;	}
	DWORD	GetRate()				{ return rate;		}

private:
	AudioCodec::Type codec;
	DWORD		 rate;
};

class AudioCodecFactory
{
public:
	static AudioDecoder* CreateDecoder(AudioCodec::Type codec);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec, const Properties &properties);
};

#endif
