#ifndef _FFVIDEOCODEC_H_
#define _FFVIDEOCODEC_H_
extern "C" {
#include <libavcodec/avcodec.h>
}

#include "medkit/codecs.h"
#include "medkit/video.h"
#include <list>

// ---------------------------------------------------------------------------
// Mapping AVCodecID -> Codec::Type medkit
// ---------------------------------------------------------------------------
inline bool MapVideoCodec( enum AVCodecID id, VideoCodec::Type & out )
{
    switch( id )
    {
        case AV_CODEC_ID_H264:  out = VideoCodec::H264;      return true;
        case AV_CODEC_ID_H263:  out = VideoCodec::H263_1996; return true;
        case AV_CODEC_ID_H263P: out = VideoCodec::H263_1998; return true;
        case AV_CODEC_ID_MPEG4: out = VideoCodec::MPEG4;     return true;
        case AV_CODEC_ID_VP8:   out = VideoCodec::VP8;       return true;
        default:                                             return false;
    }
}


class FfVideoEncoder : public VideoEncoder
{
public:
	FfVideoEncoder(const Properties& properties, enum AVCodecID av_codec, enum VideoCodec::Type codec_id);
	virtual ~FfVideoEncoder();
	virtual VideoFrame* EncodeFrame(BYTE *in, DWORD len);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width, int height);
	virtual int SetFrameRate(int fps, int kbits, int intraPeriod);

protected:
	int OpenCodec();

	// Construit l'info de packetisation RTP de `frame` (déjà rempli). Défaut :
	// schéma H263 (saut du start code 2 octets + préfixe RFC 2429). Les codecs
	// à packetisation RTP propre (VP8...) la redéfinissent dans leur sous-classe.
	virtual void PacketizeFrame();

	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVFrame		*picture;
	int		bitrate;
	int		fps;
	int		format;
	int		opened;
	int		intraPeriod;
	VideoFrame	*frame;
	VideoCodec::Type type;


	//Hardware acceleration
	AVFrame *hw_frame;
};

class FfVideoDecoder : public VideoDecoder
{
public:
	FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id);
	virtual ~FfVideoDecoder();
	virtual int Decode(BYTE *in,DWORD len);
	// Dépaquetisation par défaut : accumule le payload brut puis décode sur 'last'.
	// Les codecs à dépaquetisation RTP spécifique (H264, H263+, VP8...) la
	// redéfinissent dans leur propre classe (cf. h264decoder, h263codec, vp8decoder).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);
	virtual int GetWidth()		{ return ctx->width;		};
	virtual int GetHeight()		{ return ctx->height;		};
	virtual BYTE* GetFrame();
	virtual bool  IsKeyFrame()	{ return picture->key_frame;	};

	AVFrame * GetAVFrame() { return picture; }
protected:
	// Accessibles aux décodeurs dérivés pour leur dépaquetiseur (DecodePacket).
	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVCodecParserContext *parser_ctx;
	AVFrame		*picture;
	BYTE*		buffer;
	DWORD		bufLen;
	DWORD 		bufSize;
	BYTE*		frame;
	DWORD		frameSize;
	BYTE		src;
	VideoCodec::Type type;

};

#endif
