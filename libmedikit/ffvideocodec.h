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
        case AV_CODEC_ID_AV1:   out = VideoCodec::AV1;       return true;
        default:                                             return false;
    }
}


class FfVideoEncoder : public VideoEncoder
{
public:
	// tryHW : essaye d'abord un encodeur matériel VAAPI pour ce codec ; en cas
	// d'indisponibilité (pas de device, driver refusant profil/résolution...)
	// l'encodeur logiciel ffmpeg est utilisé en repli.
	// codec_name : si non nul, force ce nom d'encodeur ffmpeg (par ex.
	// "libsvtav1" plutôt que "libaom-av1", que avcodec_find_encoder()
	// retournerait par défaut pour AV_CODEC_ID_AV1) dans la branche logicielle
	// de SelectCodec(). Laisser à nullptr pour le comportement par défaut
	// (utilisé par H264/VP8, où il n'y a pas d'ambiguïté de backend).
	FfVideoEncoder(const Properties& properties, enum AVCodecID av_codec, enum VideoCodec::Type codec_id,
	               bool tryHW = false, const char* codec_name = nullptr);
	virtual ~FfVideoEncoder();
	virtual VideoFrame* EncodeFrame(PictPtr pic);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width, int height);
	virtual int SetFrameRate(int fps, int kbits, int intraPeriod);

protected:
	int OpenCodec();

	// Un AVCodecContext ffmpeg ne se rouvre pas : ferme le codec et réalloue
	// un contexte vierge sur le même encodeur, en conservant le device VAAPI.
	void CloseCodec();

	// Réouverture à chaud aux dimensions courantes (reconfiguration).
	int ReopenCodec();

	// Choisit l'encodeur (VAAPI si tryHW et utilisable, sinon l'encodeur
	// logiciel ffmpeg par défaut, ou codec_name si fourni) et alloue le contexte.
	bool SelectCodec(bool tryHW);

	// Nom d'encodeur logiciel forcé (cf. constructeur), ou nullptr.
	const char* codecName;

	// Bascule définitive sur l'encodeur logiciel après un échec VAAPI,
	// et rouvre aux dimensions courantes.
	int FallbackToSoftware();

	bool IsHWAccelerated() const	{ return ctx && ctx->hw_device_ctx; }

	// Réglages spécifiques au codec (rate control, profil, options privées),
	// appelés par OpenCodec() juste avant avcodec_open2(). Défaut : quantizer
	// fixe historique (H263, MPEG4, FLV1...).
	virtual void ConfigureContext();

	// Construit l'info de packetisation RTP de `frame` (déjà rempli). Défaut :
	// schéma H263 (saut du start code 2 octets + préfixe RFC 2429). Les codecs
	// à packetisation RTP propre (H264, VP8...) la redéfinissent dans leur
	// sous-classe.
	virtual void PacketizeFrame();

	enum AVCodecID	avCodecId;
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
	int64_t		pts;
	bool		hwFailed;	// l'init VAAPI a déjà échoué : ne plus réessayer
	bool		forceIntra;	// FPU demandé : forcer une I-frame au prochain EncodeFrame


	//Hardware acceleration
	AVFrame *hw_frame;
};

class FfVideoDecoder : public VideoDecoder
{
public:
	// codec_name : si non nul, force ce nom de décodeur ffmpeg. Laisser à
	// nullptr pour le comportement par défaut (avcodec_find_decoder par ID) ;
	// utile pour être explicite plutôt que de dépendre de l'ordre de
	// résolution par défaut de ffmpeg (ex : AV1 -> "libdav1d").
	FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id, const char* codec_name = nullptr);
	virtual ~FfVideoDecoder();
	virtual int Decode(BYTE *in,DWORD len);
	// Dépaquetisation par défaut : accumule le payload brut puis décode sur 'last'.
	// Les codecs à dépaquetisation RTP spécifique (H264, H263+, VP8...) la
	// redéfinissent dans leur propre classe (cf. h264decoder, h263codec, vp8decoder).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);
	virtual int GetWidth()		{ return ctx->width;		};
	virtual int GetHeight()		{ return ctx->height;		};
	virtual PictPtr GetFrame();
	virtual bool  IsKeyFrame()	{ return picture && picture->GetAVFrame() ? picture->GetAVFrame()->key_frame : false; };

	// cf. FfAudioDecoder::IsCodecAvailable : primitive de disponibilité ffmpeg
	// pour les codecs vidéo adossés à libavcodec.
	static bool IsCodecAvailable(enum AVCodecID id, const char* preferredName = nullptr);

protected:
	// Accessibles aux décodeurs dérivés pour leur dépaquetiseur (DecodePacket).
	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVCodecParserContext *parser_ctx;
	PictPtr		picture;
	BYTE*		buffer;
	DWORD		bufLen;
	DWORD 		bufSize;
	BYTE		src;
	VideoCodec::Type type;

};

#endif
