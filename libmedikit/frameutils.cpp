#include <asterisk/frame.h>
#include <asterisk/utils.h>
#include "medkit/astcpp.h"
#include "medkit/media.h"
#include "medkit/audio.h"
#include "medkit/video.h"
#include "medkit/text.h"
#include "medkit/codecs.h"
#include "medkit/log.h"
#include "astmedkit/frameutils.h"

int AstFormatToCodecList(int format, AudioCodec::Type codecList[], unsigned int maxSize)
{
    int i = 0;
    
    if ( i < maxSize && (format & AST_FORMAT_ULAW) )
    {
        codecList[i++] = AudioCodec::PCMU; 
    }
    
    if ( i < maxSize && (format & AST_FORMAT_ALAW) )
    {
        codecList[i++] = AudioCodec::PCMA; 
    }
    
    if ( i < maxSize && (format & AST_FORMAT_AMRNB) )
    {
        codecList[i++] = AudioCodec::AMR; 
    }
    
	    if ( i < maxSize && (format & AST_FORMAT_SLINEAR) )
    {
        codecList[i++] = AudioCodec::SLIN; 
    }
	
    return i;
}

int AstFormatToCodecList(int format, VideoCodec::Type codecList[], unsigned int maxSize)
{
    int i = 0;
    
    if ( i < maxSize && (format & AST_FORMAT_H264) )
    {
        codecList[i++] = VideoCodec::H264; 
    }
    
    if ( i < maxSize && (format & AST_FORMAT_H263_PLUS) )
    {
        codecList[i++] = VideoCodec::H263_1998; 
    }
    
    if ( i < maxSize && (format & AST_FORMAT_H263) )
    {
        codecList[i++] = VideoCodec::H263_1996; 
    }
    
    return i;
}

int CodecToAstFormat( AudioCodec::Type ac, int & fmt )
{
    switch(ac)
    {
	case AudioCodec::PCMU:
	    fmt |= AST_FORMAT_ULAW;
	    break;

	case AudioCodec::PCMA:
	    fmt |= AST_FORMAT_ALAW;
	    break;

	case AudioCodec::AMR:
	    fmt |= AST_FORMAT_AMRNB;
	    break;

	case AudioCodec::SLIN:
	    fmt |= AST_FORMAT_SLINEAR;
	    break;

		
	default:
	    return 0;
    }
    return 1;
}

int CodecToAstFormat( VideoCodec::Type vc, int & fmt )
{
    switch(vc)
    {
	case VideoCodec::H264:
	    fmt |= AST_FORMAT_H264;
	    break;

	case VideoCodec::H263_1996:
	    fmt |= AST_FORMAT_H263;
	    break;

	case VideoCodec::H263_1998:
	    fmt |= AST_FORMAT_H263_PLUS;
	    break;

	default:
	    return 0;
    }
    return 1;
}

bool MediaFrameToAstFrame(const MediaFrame * mf, struct ast_frame & astf)
{
	return MediaFrameToAstFrame2(mf, (MediaFrame::RtpPacketization *) NULL, false, astf, NULL, 0);
}

bool MediaFrameToAstFrame2(const MediaFrame * mf, MediaFrame::RtpPacketization * rtppak, bool first, struct ast_frame & astf, void * buffer, int len)
{
	static const char *MP4PLAYSRC = "mp4play";
	AudioFrame * af;
	VideoFrame * vf;
	TextFrame  * tf;
	
	memset(&astf, 0, sizeof(astf));
	astf.src = MP4PLAYSRC;
	astf.subclass = 0;
	
	switch( mf->GetType() )
	{
		case MediaFrame::Audio:
			af = (AudioFrame *) mf;
			astf.frametype = AST_FRAME_VOICE;
			if ( ! CodecToAstFormat(af->GetCodec(), astf.subclass ) )
			{
				Debug("Codec %s is not supported by asterisk.\n", AudioCodec::GetNameFor(af->GetCodec()) );
				return false;
			}
			astf.samples = af->GetDuration();
			break;
			
		case MediaFrame::Video:
			vf = (VideoFrame *) mf;
			astf.frametype = AST_FRAME_VIDEO;
			if ( ! CodecToAstFormat(vf->GetCodec(), astf.subclass ) )
			{
				Debug("Codec %s is not supported by asterisk.\n", VideoCodec::GetNameFor(vf->GetCodec()) );
				return false;
			}
			if (rtppak->IsMark() ) astf.subclass |= 0x1;
			if ( first ) 
				astf.samples = mf->GetDuration() * 90;
			else
				astf.samples = 0;
			break;
			
		case MediaFrame::Text:
			/* todo = passer un argument suppa */
			tf = (TextFrame *) mf;
			astf.frametype = AST_FRAME_TEXT;
		   	astf.subclass = AST_FORMAT_RED;
			if (rtppak->IsMark() ) astf.subclass |= 0x1;
			break;
		
		default:
			Debug("Media %s is not supported by asterisk.\n", MediaFrame::TypeToString(mf->GetType()) );
			return false;
	}
	
	astf.flags = 0; /* nothing is malloc'ed */
	if (rtppak == NULL)
	{
		astf.data = mf->GetData();
		astf.datalen = mf->GetLength();
	}
	else
	{
		if ( rtppak->GetPos() + rtppak->GetSize() > mf->GetLength() )
		{
			Debug("Inconsistent RTP packet: is defined out of the frame bounds.\n");
			Debug("Frame length is: %u bytes.\n", mf->GetLength());
			Debug("Packet end is at %u bytes.\n", rtppak->GetPos() + rtppak->GetSize());
			return false;
		}
		if (rtppak->GetPrefixData() == NULL || rtppak->GetPrefixLen() == 0)
		{			 
			astf.data = mf->GetData() + rtppak->GetPos();
			astf.datalen = rtppak->GetSize();
		}
		else
		{
			if ( rtppak->GetSize() + rtppak->GetPrefixLen() > len)
				return false;
			
			BYTE * buff2 = (BYTE *) buffer;
			memcpy(buff2, rtppak->GetPrefixData(), rtppak->GetPrefixLen());
			buff2 += rtppak->GetPrefixLen();
			memcpy(buff2, mf->GetData() + rtppak->GetPos(), rtppak->GetSize());
			astf.data = buffer;
			astf.datalen = rtppak->GetSize() + rtppak->GetPrefixLen();
		}
	}		

	// Copy frame timestamp 
	astf.ts = mf->GetTimeStamp();
	//change the delivery time for avoiding WebRTC bug with timestamp to zero
	astf.delivery = ast_tvnow();
	return true;	
}
