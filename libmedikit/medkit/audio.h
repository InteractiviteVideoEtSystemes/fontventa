#ifndef _AUDIO_H_
#define _AUDIO_H_
#include "config.h"
#include "media.h"
#include "codecs.h"

class AudioEncoder
{
public:
	virtual ~AudioEncoder() {}
	virtual int   Encode(SWORD *in,int inLen,BYTE* out,int outLen)=0;
	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	virtual DWORD GetClockRate()=0;
	virtual bool  GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp=""; return false; };

	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;
};

class AudioDecoder
{
public:
	virtual ~AudioDecoder() {}
	virtual int   Decode(BYTE *in,int inLen,SWORD* out,int outLen)=0;
	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	virtual bool  GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp=""; return false; };
	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;
};

class AudioFrame : public MediaFrame
{
public:
	AudioFrame(AudioCodec::Type codec,DWORD rate, bool owns = true) : MediaFrame(MediaFrame::Audio,2048, owns)
	{
		//Store codec
		this->codec = codec;
		//Set default rate
		this->rate = rate;

		switch(codec)
		{
			// Codecs « sample-based » : chaque octet est un échantillon
			// indépendant, on découpe donc à 160 octets = 20 ms de RTP.
			case AudioCodec::PCMA:
			case AudioCodec::PCMU:
			case AudioCodec::G722:
				packetization = 160;
				break;
			// Codecs « frame-based » (Opus, AMR, GSM, Speex, AAC…) :
			// une trame codée est une unité indivisible qui doit tenir dans
			// UN paquet RTP. On met une valeur large (plafonnée au mtu par
			// Packetize) pour ne jamais fragmenter une trame en octets — sans
			// quoi le flux (Opus notamment) devient indécodable côté pair.
			default:
				packetization = 0xFFFF;
				break;
		}
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

		frame->packetization = this->packetization;
		//Return it
		return (MediaFrame*)frame;
	}

	AudioCodec::Type GetCodec() const			{ return codec;		}
	void	SetCodec(AudioCodec::Type codec)	{ this->codec = codec;	}
	DWORD	GetRate() const				{ return rate;		}

	virtual bool Packetize(unsigned int mtu);

private:
	AudioCodec::Type codec;
	DWORD		 rate;
	unsigned int packetization;
};

class AudioInput
{
public:
	virtual DWORD GetNativeRate()=0;
	virtual DWORD GetRecordingRate()=0;
	virtual int RecBuffer(SWORD *buffer,DWORD size)=0;
	virtual void  CancelRecBuffer()=0;
	virtual int StartRecording(DWORD samplerate)=0;
	virtual int StopRecording()=0;
};

class AudioOutput
{
public:
	virtual DWORD GetNativeRate()=0;
	virtual DWORD GetPlayingRate()=0;
	virtual int PlayBuffer(SWORD *buffer,DWORD size,DWORD frameTime)=0;
	virtual int StartPlaying(DWORD samplerate)=0;
	virtual int StopPlaying()=0;
};

class AudioCodecFactory
{
public:
	static AudioDecoder* CreateDecoder(AudioCodec::Type codec);
	// Variante avec extradata (AudioSpecificConfig/esds) : requise par les codecs
	// dont le décodeur a besoin de sa configuration (AAC des MP4). Ignoré par les
	// codecs qui n'en ont pas besoin.
	static AudioDecoder* CreateDecoder(AudioCodec::Type codec, const BYTE* extradata, DWORD extradataSize);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec, const Properties &properties);

	// Liste des codecs audio réellement disponibles (candidats instanciables
	// filtrés par AudioCodec::IsSupported). Calculée une fois, mémoïsée,
	// thread-safe. Ordre = priorité de préférence.
	static const std::vector<AudioCodec::Type>& GetSupportedCodecs();
};

#endif
