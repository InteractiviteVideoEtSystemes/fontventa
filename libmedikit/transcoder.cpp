extern "C"
{
#include <asterisk/frame.h>
}
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "medkit/astcpp.h"
#include "medkit/transcoder.h"
#include "medkit/log.h"
#include "medkit/video.h"
#include "medkit/videorescaler.h"
#include "astmedkit/frameutils.h"
#include "astmedkit/mp4format.h"

struct VideoTranscoder
{
    VideoTranscoder(void * ctxdata, VideoTranscoderCb cb, VideoCodec::Type outputCodec,
		    unsigned int width, unsigned int height, int fps, int kbits, int intraPeriod);
    ~VideoTranscoder();

    bool EncoderOpen();
    bool SetInputCodec(VideoCodec::Type codec);
    int  ProcessFrame(const ast_frame * f);
    bool GetDecodedPicParams(VideoCodec::Type * codec, DWORD * width, DWORD * height);

    VideoDecoder * decoder;
    VideoEncoder * encoder;
    VideoRescaler  rescaler;

    unsigned int width_out;
    unsigned int height_out;
    int fps;
    int kbits;
    int intraPeriod;
    int videoSeqNo;

    VideoTranscoderCb cb;
    void * ctxdata;
};

VideoTranscoder::VideoTranscoder(void * ctxdata, VideoTranscoderCb cb, VideoCodec::Type outputCodec,
				 unsigned int width, unsigned int height, int fps, int kbits, int intraPeriod)
{
    this->ctxdata     = ctxdata;
    this->cb          = cb;
    this->width_out   = width;
    this->height_out  = height;
    this->fps         = fps;
    this->kbits       = kbits;
    this->intraPeriod = intraPeriod;
    videoSeqNo        = 0xFFFF;
    decoder           = NULL;
    encoder           = VideoCodecFactory::CreateEncoder(outputCodec);
}

VideoTranscoder::~VideoTranscoder()
{
    if (encoder) delete encoder;
    if (decoder) delete decoder;
}

bool VideoTranscoder::EncoderOpen()
{
    if (encoder == NULL) return false;
    // SetSize ouvre le codec : la cadence et le débit doivent être connus avant.
    encoder->SetFrameRate(fps, kbits, intraPeriod);
    return encoder->SetSize(width_out, height_out) > 0;
}

bool VideoTranscoder::SetInputCodec(VideoCodec::Type codec)
{
    if (decoder != NULL)
    {
	if (decoder->type == codec) return true;
	delete decoder;
	decoder = NULL;
    }
    decoder = VideoCodecFactory::CreateDecoder(codec);
    return decoder != NULL;
}

int VideoTranscoder::ProcessFrame(const ast_frame * f)
{
    VideoCodec::Type codec;

    if (f == NULL || f->frametype != AST_FRAME_VIDEO) return -1;
    if (!AstFormatToCodec(f->subclass, codec)) return -1;
    if (!SetInputCodec(codec)) return -1;

    bool mark = (f->subclass & 0x01) != 0;
    int lost = 0;
    if (videoSeqNo != 0xFFFF && f->seqno != 0xFFFF && f->seqno != ((videoSeqNo + 1) & 0xFFFF))
    {
	Log("-Transcoder: video packet lost, seqno=%d expected=%d\n", f->seqno, (videoSeqNo + 1) & 0xFFFF);
	lost = 1;
    }
    videoSeqNo = f->seqno;

    if (!decoder->DecodePacket(AST_FRAME_GET_BUFFER(f), f->datalen, lost, mark)) return -1;
    if (!mark) return 0;

    PictPtr pic = decoder->GetFrame();
    if (!pic) return -1;

    PictPtr scaled = rescaler.Rescale(pic, width_out, height_out, false);
    if (!scaled) return -2;

    if (encoder == NULL || cb == NULL) return 1;

    VideoFramePtr out = encoder->EncodeFrame(scaled);
    if (!out) return -2;

    cb(ctxdata, out->GetCodec(), (const char *) out->GetData(), out->GetLength());
    return 1;
}

bool VideoTranscoder::GetDecodedPicParams(VideoCodec::Type * codec, DWORD * width, DWORD * height)
{
    if (decoder == NULL || decoder->GetWidth() <= 0 || decoder->GetHeight() <= 0) return false;
    *codec  = decoder->type;
    *width  = decoder->GetWidth();
    *height = decoder->GetHeight();
    return true;
}

struct VideoTranscoder * VideoTranscoderCreate(void * ctxdata, char * format, VideoTranscoderCb cb)
{
    VideoCodec::Type output;

    if (format == NULL) return NULL;

    if (strncasecmp(format, "h263", 4) == 0)
    {
	output = VideoCodec::H263_1996;
    }
    else if (strncasecmp(format, "h264", 4) == 0)
    {
	output = VideoCodec::H264;
    }
    else
    {
	Error("-Transcoder: unsupported output format %s\n", format);
	return NULL;
    }

    unsigned int width_out = 352, height_out = 288;
    int fps = -1, kbits = -1, intraPeriod = -1;

    // Format : <codec>[@<taille>][/fps=<n>][/kb=<n>][/gs=<n>] ; taille = qcif, cif, vga.
    char * i = strchr(format, '@');
    while (i)
    {
	i++;
	if (strncasecmp(i, "qcif", 4) == 0)
	{
	    width_out = 176;
	    height_out = 144;
	}
	else if (strncasecmp(i, "cif", 3) == 0)
	{
	    width_out = 352;
	    height_out = 288;
	}
	else if (strncasecmp(i, "vga", 3) == 0)
	{
	    width_out = 640;
	    height_out = 480;
	}
	else if (strncasecmp(i, "fps=", 4) == 0)
	{
	    fps = atoi(i + 4);
	}
	else if (strncasecmp(i, "kb=", 3) == 0)
	{
	    kbits = atoi(i + 3);
	}
	else if (strncasecmp(i, "gs=", 3) == 0)
	{
	    intraPeriod = atoi(i + 3);
	}
	i = strchr(i, '/');
    }

    VideoTranscoder * vtc = new VideoTranscoder(ctxdata, cb, output, width_out, height_out, fps, kbits, intraPeriod);

    if (!vtc->EncoderOpen())
    {
	Error("-Transcoder: error opening %s encoder\n", VideoCodec::GetNameFor(output));
	delete vtc;
	return NULL;
    }

    return vtc;
}

int VideoTranscoderDestroy(struct VideoTranscoder * vtc)
{
    if (vtc) delete vtc;
    return 1;
}

int VideoTranscoderProcessFrame(struct VideoTranscoder * vtc, const ast_frame * f)
{
    if (vtc == NULL) return -1;
    return vtc->ProcessFrame(f);
}

int VideoTranscoderGetDecodedPicParams(struct VideoTranscoder * vtc, int * codec, DWORD * width, DWORD * height)
{
    VideoCodec::Type c2;

    if (vtc == NULL || !vtc->GetDecodedPicParams(&c2, width, height)) return 0;
    *codec = c2;
    return 1;
}
