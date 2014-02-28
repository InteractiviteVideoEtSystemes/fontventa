#include "medkit/transcoder.h"
#include "medkit/video.h"
#include "medkit/framescaler.h"

struct VideoTranscoder
{    
    VideoTranscoder(void  * ctxdata, unsigned int width_out, unsigned int height_out,
		    VideoCodec::Type outputcodec, 
		    unsigned int bitrate, unsigned int fps, unsigned gob_size);
				 
    ~VideoTranscoder();
    
    bool EncoderOpen();
    bool ComputeFps(time_t t)    { return false; }

    bool SetInputCodec(VideoCodec::Type codec);
    
    bool ProcessFrame(VideoFrame * f, int lost, int last);
    int  HandleResize();
    void SetListener(MediaFrame::Listener * listener) { this->listener = listener; }
    
    bool GetDecodedPicParams( VideoCodec::Type * codec, DWORD * width, DWORD * height);
    
    
    VideoDecoder *decoder;
    FrameScaler  *scaler;
    VideoEncoder *encoder;

    unsigned int bitrate_out; 
    unsigned int bitrate_in;
    unsigned int bitrate_in_tmp;
    
    unsigned int numPixSrc;
    unsigned int numPixDst;
    
    unsigned int gob_size;
    unsigned int width_out;
    unsigned int height_out;
    unsigned int resizeWidth;
    unsigned int resizeHeight;
    unsigned int fps_in;
    unsigned int fps_in_tmp;
    unsigned int fps_out;
    unsigned int fps_out_max;
    
    BYTE * decodedPic;
    DWORD  decodedPicSize;
    
    MediaFrame::Listener * listener;
    
    VideoTranscoderCb cb;
    void * ctxdata;
};


VideoTranscoder::VideoTranscoder(void  * ctxdata, unsigned int width_out, unsigned int height_out, VideoCodec::Type outputcodec, 
				 unsigned int bitrate, unsigned int fps, unsigned gob_size)
{
    this->ctxdata	= ctxdata;
    bitrate_out         = bitrate;
    bitrate_in          = 0;
    this->gob_size      = gob_size;
    this->width_out = width_out;
    this->height_out = height_out;
    
    /* we will measure it */
    fps_in = 0;
    fps_in_tmp = 0;
    
    fps_out = 15;
    fps_out_max = fps;
    
    encoder = VideoCodecFactory::CreateEncoder(outputcodec);
    decoder = NULL;
    scaler = NULL;
    listener = NULL;
    decodedPic = NULL;
    decodedPicSize = 0;
    
    numPixSrc = 0;
    numPixDst = width_out * height_out;
    resizeWidth = 0;
    resizeHeight = 0;
}

#if 0
void av_log_asterisk_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    if ( option_debug > 2 )
    {
        char msg[1024];
        vsnprintf(msg,1024,fmt,vl);

        AVClass* avc= ptr ? *(AVClass**)ptr : NULL;
        if (avc)
	    ast_log(LOG_DEBUG,"[%s @ %p] %s",avc->item_name(ptr), avc, msg);
        else 
	    ast_log(LOG_DEBUG, msg);
    }
}
#endif

int VideoTranscoderDestroy(struct VideoTranscoder *vtc)
{
    if (vtc) delete vtc;
    return 1;
}

VideoTranscoder::~VideoTranscoder()
{
    if (encoder) delete encoder;
    if (decoder) delete decoder;
    if (scaler) delete scaler;
}

bool VideoTranscoder::EncoderOpen()
{
    if (encoder)
    {
	encoder->SetSize(width_out, height_out);
	encoder->SetFrameRate(fps_out, bitrate_out, gob_size);
	return true;
    }
    return false;
}

bool VideoTranscoder::SetInputCodec(VideoCodec::Type codec)
{
    if (decoder != NULL)
    {
	// If no change is needed, just return.
	if ( decoder->type == codec ) return true;
	
	delete decoder;
	decoder = NULL;
    }
    
    decoder = VideoCodecFactory::CreateDecoder(codec);
    
    return (decoder != NULL);
}

bool VideoTranscoder::ProcessFrame(VideoFrame * f, int lost, int last)
{
    VideoFrame * f_out = NULL;
    time_t t = time(NULL);
    bool needAdjust = false;
    BYTE * srcY, * srcU, * srcV;
    BYTE * dstY, * dstU, * dstV;    
    
    if ( decoder == NULL || decoder->type != f->GetCodec() )
    {
	SetInputCodec( f->GetCodec() );
    }
        
    if ( decoder != NULL )
    {
	int res = decoder->DecodePacket(f->GetData(), f->GetLength(), lost, last);
	
	if (res == 2) /* 2 means image complete */
	{
	    if ( ComputeFps( t ) )
	    {
		needAdjust = true;
	    }
	    
	    switch ( HandleResize() )
	    {
	        case 0:
		    delete f_out;
		    return false; // drop frame
		    
		case 1:
		    srcY = decoder->GetFrame();
		    srcU = srcY + numPixSrc;
		    srcV = srcU + numPixSrc/4;
		    dstY = decodedPic;
		    dstU = decodedPic + numPixDst;
		    dstV = dstU + numPixDst/4;
		    scaler->Resize(srcY,srcU,srcV,dstY,dstU,dstV);
		    break;
		
		case 2:
		    dstY = decoder->GetFrame();
		    decodedPicSize = numPixDst + numPixDst/4;
		    break;
		    
		default:
		    delete f_out;
		    return false; // drop frame
		
	    
	    }
	    if ( needAdjust )
		EncoderOpen();
	    
	    if ( encoder != NULL && ( listener != NULL || cb != NULL ))
	    {
		f_out = encoder->EncodeFrame( dstV, decodedPicSize );
	    }
	    
	    if (listener)
	    {
		listener->onMediaFrame( *f_out );
	    }
	    
	    if ( cb != NULL ) cb( ctxdata, f_out->GetCodec(), (const char *) f_out->GetData(),
				  f_out->GetLength() );
	    
	    delete f_out;
	}
    }
}


bool VideoTranscoder::GetDecodedPicParams( VideoCodec::Type * codec, DWORD * width, DWORD * height)
{
    if ( decoder != NULL)
    {
        if ( decoder->GetWidth() > 0 && decoder->GetHeight() > 0 )
	{
	    (*codec) = decoder->type;
	    *width = decoder->GetWidth();
	    *height = decoder->GetHeight();
	    return true;
	}
    }
    return false;
}

int VideoTranscoder::HandleResize()
{
    if (decoder == NULL || encoder == NULL) return 0;
    
    /* If already resizing that size */
    if (scaler != NULL && decoder->GetWidth() == resizeWidth && decoder->GetHeight() == resizeHeight)
	return 1;

   numPixSrc = decoder->GetWidth() * decoder->GetHeight();
    /* if encoder and decoder have the same format */
    if (decoder->GetWidth() == width_out && decoder->GetHeight() == height_out )
    {
	/* Nothing to do */
        return 2;
    }    
    else
    {
	/* Invalid picture or decoder context. Ignore */
	if (decoder->GetWidth() == 0 || decoder->GetHeight() == 0) return 0;
	
	if ( scaler == NULL ) scaler = new FrameScaler();

	if ( scaler->SetResize( decoder->GetWidth(), decoder->GetHeight(), decoder->GetWidth(),
				(int) width_out, (int) height_out, (int) width_out ) )
	{
	    resizeWidth = decoder->GetWidth();
	    resizeHeight = decoder->GetHeight();
	    
	    if (decodedPic) free(decodedPic);
	    decodedPicSize = resizeWidth*resizeHeight + (resizeWidth*resizeHeight) / 2;

	    decodedPic = (BYTE *) malloc(decodedPicSize);
	    
	    return 1;
	}
    }	
    return 0;
}

struct VideoTranscoder * VideoTranscoderCreate(struct ast_channel *channel,char *format)
{
    /* Check params */
    VideoCodec::Type output;
    struct VideoTranscoder *vtc;
    
    if ( strncasecmp(format,"h263",4) == 0 )
    {
        output = VideoCodec::H263_1996;
    }
    else if ( strncasecmp(format,"h264",4) == 0 )
    {
        output = VideoCodec::H264;
    }
    else
    {
 	/* Only h263 or h264 output by now*/
	return NULL;
    }

    /* Get first parameter */
    char *i = strchr(format,'@');
    int picsize = 0, qMin = -1, qMax = -1, fps = -1, bitrate = -1;
    int gob_size_out;
    unsigned int width_out = 352, height_out = 288;
    /* Parse params */
    while (i)
    {
	/* skip separator */
	i++;

	/* compare */
	if (strncasecmp(i,"qcif",4) == 0)
	{
	    /* Set qcif */
	    width_out = 176;
	    height_out = 144;
	    picsize = 0;
	} 
	else if (strncasecmp(i,"cif",3)==0) 
	{
			/* Set cif */
	    width_out = 352;
	    height_out = 288;
	    picsize = 1;
	} 
	else if (strncasecmp(i,"vga",3)==0) 
	{
	    /* Set VGA */
	    width_out = 640;
	    height_out = 480;
	    picsize = 2;
	}
	else if (strncasecmp(i,"fps=",4)==0) 
	{
	    /* Set fps */
	    fps = atoi(i+4);
	} 
	else if (strncasecmp(i,"kb=",3)==0)
	{
	    /* Set bitrate */
	    bitrate = atoi(i+3)*1024;
	} 
	else if (strncasecmp(i,"qmin=",5) == 0) 
	{
		/* Set qMin */
		qMin = atoi(i+5);
	}
	else if (strncasecmp(i,"qmax=",5)==0) {
			/* Set qMax */
		qMax = atoi(i+5);
	} 
	else if (strncasecmp(i,"gs=",3)==0) {
			/* Set gop size */
		gob_size_out = atoi(i+3);
	}

	/* Find next param*/
	i = strchr(i,'/');
    }

    /* Create transcoder */
    // VideoTranscoder(void  * ctxdata, unsigned int width_out, unsigned int height_out,
    //                VideoCodec::Type outputcodec,
    //                unsigned int bitrate, unsigned gob_size)
    vtc = new VideoTranscoder(channel, width_out, height_out, output, fps, bitrate, gob_size_out);

    if ( vtc == NULL ) 
    {
    	ast_log(LOG_ERROR,"-Transcoder allocation failed\n" );
         return NULL ; 
    }

    /* If not opened correctly */
    if (! vtc->EncoderOpen() )
    {
	/* Error */
	ast_log(LOG_ERROR,"-Transcoder: Error opening encoder\n");
	/* Destroy it */
	delete vtc;
	/* Exit */
	return NULL;	
    }

    /* Return encoder */
    return vtc;
}

int VideoTranscoderGetDecodedPicParams( struct VideoTranscoder *vtc, int * codec, DWORD * width, DWORD *height )
{
    VideoCodec::Type c2;
    
    int ret = vtc->GetDecodedPicParams(&c2, width, height );
    if (ret)
    {
	*codec = c2;
    }
    return ret;
}

