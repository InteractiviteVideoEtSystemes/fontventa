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
	virtual VideoFramePtr EncodeFrame(PictPtr pic);
	virtual int FastPictureUpdate();
	virtual int SetSize(int width, int height);
	virtual int SetFrameRate(int fps, int kbits, int intraPeriod);

	// cf. FfAudioEncoder::IsCodecAvailable. Ne teste que la branche LOGICIELLE
	// de SelectCodec() : l'encodeur VAAPI est opportuniste (repli logiciel), il
	// ne conditionne donc pas le support du codec.
	static bool IsCodecAvailable(enum AVCodecID id, const char* preferredName = nullptr);

protected:
	int OpenCodec();

	// Un AVCodecContext ffmpeg ne se rouvre pas : ferme le codec et réalloue
	// un contexte vierge sur le même encodeur, en conservant le device VAAPI.
	void CloseCodec();

	// Vide l'encodeur avant sa fermeture (trame NULL puis paquets jusqu'à
	// EOF). Fermer SVT-AV1 avec des images en vol bloque son destructeur :
	// il joint des threads internes dont l'un attend un buffer libre que
	// seul ce drainage rend (deadlock observé au raccroché, 2026-09-01).
	void DrainCodec();

	// Réouverture à chaud aux dimensions courantes (reconfiguration).
	int ReopenCodec();

	// Faut-il rouvrir pour que `bitrate` prenne effet ? Réponse ASYMÉTRIQUE,
	// pour les codecs dont le wrapper ffmpeg ignore ctx->bit_rate à chaud
	// (libvpx, SVT-AV1) : la réouverture est leur seul levier, mais elle coûte
	// une trame clé. Baisse : tout de suite, c'est le sens qui compte quand le
	// réseau se ferme. Hausse : par paliers, sinon la rampe de la boucle
	// d'adaptation la déclenche en continu.
	bool ShouldReopenForBitrate() const;

	// Faut-il rouvrir pour que `fps` prenne effet ? Toujours : la cadence n'est
	// lue qu'à l'ouverture (`time_base`, `rc_buffer_size`, `gop_size` en
	// dépendent), et AUCUN encodeur ne la reconfigure à chaud —
	// x264_encoder_reconfig ne la couvre pas plus que VAAPI ou SVT-AV1. Un écart
	// de plus de 20 % fausse le budget par image au point de diviser le débit
	// réel ; en deçà, la trame clé coûterait plus qu'elle ne corrige.
	bool ShouldReopenForFps() const;

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

	// Construit l'info de packetisation RTP de `frame` (déjà remplie). Défaut :
	// schéma H263 (saut du start code 2 octets + préfixe RFC 2429). Les codecs
	// à packetisation RTP propre (H264, VP8...) la redéfinissent dans leur
	// sous-classe.
	virtual void PacketizeFrame(VideoFrame& frame);

	enum AVCodecID	avCodecId;
	const AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVFrame		*picture;
	int		bitrate;
	// Débit auquel le codec a été ouvert : référence de ShouldReopenForBitrate.
	int		openedBitrate;
	// Cadence à laquelle le codec a été ouvert : référence de ShouldReopenForFps.
	int		openedFps;
	int		fps;
	int		format;
	int		opened;
	int		intraPeriod;
	// PAS de 'VideoCodec::Type type' ici : il masquerait VideoEncoder::type
	// (medkit/video.h), que le constructeur laisserait alors non initialisé —
	// or les appelants le lisent via un VideoEncoder* (FLVEncoder.cpp).
	int64_t		pts;
	bool		hwFailed;	// l'init VAAPI a déjà échoué : ne plus réessayer
	bool		forceIntra;	// FPU demandé : forcer une I-frame au prochain EncodeFrame
	// Accélération matérielle EXIGÉE (propriété "video.hwaccel.required") : aucun
	// repli logiciel n'est autorisé ; l'ouverture échoue si VAAPI est indisponible.
	bool		requireHW;

	//Hardware acceleration
	AVFrame *hw_frame;

public:
	// true si l'encodeur tourne effectivement en VAAPI (device matériel actif).
	bool IsHardwareReady() const { return ctx && ctx->hw_device_ctx; }
};

class FfVideoDecoder : public VideoDecoder
{
public:
	// codec_name : si non nul, force ce nom de décodeur ffmpeg. Laisser à
	// nullptr pour le comportement par défaut (avcodec_find_decoder par ID) ;
	// utile pour être explicite plutôt que de dépendre de l'ordre de
	// résolution par défaut de ffmpeg (ex : AV1 -> "libdav1d").
	// requireHW : exige un décodage matériel VAAPI. Sans device VAAPI (pas de
	// /dev/dri/renderD128...), le décodeur reste « non prêt » (IsHardwareReady()
	// == false) et Decode() échoue, au lieu de retomber silencieusement en
	// logiciel comme le mode par défaut.
	FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id, const char* codec_name = nullptr,
	               bool requireHW = false);
	virtual ~FfVideoDecoder();
	virtual int Decode(BYTE *in,DWORD len);
	// true si le décodeur dispose d'un device VAAPI actif.
	bool IsHardwareReady() const { return ctx && ctx->hw_device_ctx; }
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
	// PAS de 'VideoCodec::Type type' ici : il masquerait VideoDecoder::type
	// (medkit/video.h), laissé alors non initialisé — et videostream.cpp:810
	// (comme VideoDecoderWorker/rtmpparticipant) compare
	// videoDecoder->type via un VideoDecoder*, ce qui recréait le décodeur à
	// CHAQUE paquet RTP (aucune trame jamais décodée).
	// Décodage matériel VAAPI exigé (cf. constructeur) : pas de repli logiciel.
	bool		requireHW;
};

#endif
