#include "medkit/log.h"
#include "medkit/video.h"
#include "h263/h263codec.h"
#include "h263/mpeg4codec.h"
#include "h264/h264encoder.h"
#include "h264/h264decoder.h"

bool VideoFrame::GuessIsIntra()
{
    if ( length > 0 && buffer != NULL )
    {
	switch(codec)
	{
		case VideoCodec::H263_1996:
		    if ( length > 7 && (buffer[1] & 0x10) != 0 )
		    {
			// Check PSC code to check if it is first packet of I-frame
			if ( buffer[4] == 0 && buffer[5] == 0 && ((buffer[6] & 0xFC) == 0x80))
			{
			    SetIntra(true);
			    return true;
			}
		    }
		    break;
		    
		case VideoCodec::H263_1998:
		    if ( length > 5 && (buffer[0] & 0x04) != 0 )
		    {
			if (buffer[4] & 0x02)
			{
			    SetIntra(true);
			    return true;
			}
		    }
		    break;

		case VideoCodec::H264:
		    // Check if NAL type is SEQUENCE PARAMETER SET (we could use the class to dedcode
		    // but we just make a quick check here ...
		    if ( length > 1 && (buffer[0] & 0x1f) == 0x07 )
		    {
		        SetIntra(true);
		        return true;
		    }
		    break;
		
		default:
		    // We don't know this codec so we do not decide
		    return 0;
	}
   }
   SetIntra(false);
   return false;
}
VideoDecoder* VideoCodecFactory::CreateDecoder(VideoCodec::Type codec)
{
	Log("-CreateVideoDecoder[%d,%s]\n",codec,VideoCodec::GetNameFor(codec));

	//Depending on the codec
	switch(codec)
	{
		case VideoCodec::H263_1998:
			return new H263Decoder();
		case VideoCodec::H263_1996:
			return new H263Decoder1996();
		case VideoCodec::MPEG4:
			return new Mpeg4Decoder();
		case VideoCodec::H264:
			return new H264Decoder();
		default:
			Error("Video decoder not found [%d]\n",codec);
	}
	return NULL;
}

VideoEncoder* VideoCodecFactory::CreateEncoder(VideoCodec::Type codec)
{
	//Empty properties
	Properties properties;

	//Create codec
	return CreateEncoder(codec,properties);
}


VideoEncoder* VideoCodecFactory::CreateEncoder(VideoCodec::Type codec,const Properties& properties)
{
	Log("-CreateVideoEncoder[%d,%s]\n",codec,VideoCodec::GetNameFor(codec));

	//Depending on the codec
	switch(codec)
	{
		case VideoCodec::H263_1998:
			return new H263Encoder(properties);
		case VideoCodec::H263_1996:
			return new H263Encoder1996(properties);
		case VideoCodec::MPEG4:
			return new Mpeg4Encoder(properties);
		case VideoCodec::H264:
			return new H264Encoder(properties);
		default:
			Error("Video Encoder not found\n");
	}
	return NULL;
}
