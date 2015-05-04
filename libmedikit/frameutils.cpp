#include <asterisk/frame.h>
#include "medkit/media.h"
#include "medkit/codecs.h"

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

bool MediaFrameToAstFrame(const MediaFrame * mf, ast_frame & astf)
{
	static const char *MP4PLAYSRC = "mp4play";
	AudioFrame * af;
	VideoFrame * vf;
	TextFrame  * tf;
	
	memset(&astf, 0, sizeof(astf));
	astf.src = MP4PLAYSRC;
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
			break;
			
		case MediaFrame::Video:
			vf = (VideoFrame *) mf;
			astf.frametype = AST_FRAME_VIDEO;
			if ( ! CodecToAstFormat(vf->GetCodec(), astf.subclass ) )
			{
				Debug("Codec %s is not supported by asterisk.\n", VideoCodec::GetNameFor(vf->GetCodec()) );
				return false;
			}
			break;
			
		case MediaFrame::Text:
			tf = (TextFrame *) mf;
			astf.frametype = AST_FRAME_TEXT;
			if ( ! CodecToAstFormat(tf->GetCodec(), astf.subclass ) )
			{
				Debug("Codec %s is not supported by asterisk.\n", TextCodec::GetNameFor(tf->GetCodec()) );
				return false;
			}
			break;
		
		default:
			Debug("Media %s is not supported by asterisk.\n", MediaFrame::TypeToString(mf->GetType()) );
			return false;
	}
	
	astf.flags = 0; /* nothing is malloc'ed */
	astf.data = mf->GetData();
	astf.datalen = mf->GetSize();
	return true;
}
