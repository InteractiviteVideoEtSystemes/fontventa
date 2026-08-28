#include <string.h>
#include <netinet/in.h>
#include "medkit/log.h"
#include "medkit/video.h"
#include "medkit/ffcodeclock.h"
#include "ffvideocodec.h"


// av_err2str() alloue un tableau temporaire et en prend l'adresse : invalide en C++.
static const char* AVErrToStr(int err)
{
	static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

// Retourne true si un device VAAPI a été créé et attaché à ctx->hw_device_ctx.
static bool TryVAAPI(AVCodecContext * ctx, const AVCodec *codec)
{
	// Try to enable hardware accelation
	// Check if VAAPI is available for this codec.
	bool use_vaapi = false;

    const AVCodecHWConfig *hw_config = nullptr;
    for (int i = 0; ; i++) {
        hw_config = avcodec_get_hw_config(codec, i);
        if (!hw_config) break;
        if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            hw_config->device_type == AV_HWDEVICE_TYPE_VAAPI) {
			use_vaapi = true;
            break;
        }
    }

	if (use_vaapi)
	{
		Log("FFMpeg encoder [%s] supports VAAPI hardware acceleration\n", codec->name);

		// Device VAAPI PARTAGÉ du processus : indispensable pour que décodeurs,
		// encodeurs et graphe de composition mosaïque (overlay_vaapi) échangent
		// leurs surfaces sans copie — les filtres *_vaapi refusent de mélanger
		// des trames issues de devices distincts.
		AVBufferRef *shared = Pict::GetVAAPIDevice();
		if (!shared)
		{
			Log("Shared VAAPI device unavailable. Falling back to CPU.\n");
			ctx->hw_device_ctx = nullptr;
		}
		else
		{
			ctx->hw_device_ctx = av_buffer_ref(shared);
			return ctx->hw_device_ctx != nullptr;
		}
	}
	return false;
}

// Callback get_format : impose le format matériel VAAPI au décodeur quand il est
// proposé, sinon celui-ci reste en logiciel sans jamais activer le hwaccel.
static enum AVPixelFormat GetVAAPIFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
		if (*p == AV_PIX_FMT_VAAPI)
			return *p;

	Error("Failed to get VAAPI pixel format, falling back to default\n");
	return avcodec_default_get_format(ctx, pix_fmts);
}

static void AllocateVAAPIFrame(AVCodecContext * ctx)
{
	if (ctx->hw_device_ctx) 
	{
		ctx->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
		if (!ctx->hw_frames_ctx)
		{
			Log("Failed to allocate VAAPI frame context. Falling back to CPU.\n");
			av_buffer_unref(&ctx->hw_device_ctx);
			ctx->hw_device_ctx = nullptr;
		}
		else
		{
			AVHWFramesContext *frames_ctx = (AVHWFramesContext *)ctx->hw_frames_ctx->data;

            frames_ctx->format = AV_PIX_FMT_VAAPI;
            frames_ctx->sw_format = AV_PIX_FMT_YUV420P;
            frames_ctx->width = ctx->width;
            frames_ctx->height = ctx->height;
            frames_ctx->initial_pool_size = 20;

            int ret = av_hwframe_ctx_init(ctx->hw_frames_ctx);
            if (ret < 0) {
                Log("Failed to initialize VAAPI frame context: %s. Falling back to CPU.\n", AVErrToStr(ret));
                av_buffer_unref(&ctx->hw_frames_ctx);
				av_buffer_unref(&ctx->hw_device_ctx);
				ctx->hw_device_ctx= nullptr;
            }
		}
	}
}
bool FfVideoEncoder::IsCodecAvailable(enum AVCodecID id, const char* preferredName)
{
	// Branche logicielle de SelectCodec() uniquement : nom prefere d'abord, puis
	// repli sur l'ID. Rien n'est ouvert.
	if (preferredName && avcodec_find_encoder_by_name(preferredName))
		return true;
	return avcodec_find_encoder(id) != nullptr;
}

/***********************
* FfVideoEncoder
************************/
FfVideoEncoder::FfVideoEncoder(const Properties& properties, enum AVCodecID av_codec, enum VideoCodec::Type codec_id,
                               bool tryHW, const char* codec_name)
{
	// Set default values
	frame	= NULL;
	ctx	= NULL;
	codec	= NULL;
	picture	= NULL;
	hw_frame = nullptr;
	opened	= false;
	type    = codec_id;
	format  = 0;
	avCodecId = av_codec;
	hwFailed = false;
	pts	= 0;
	forceIntra = false;
	codecName = codec_name;
	// Accélération matérielle exigée par configuration : aucun repli logiciel.
	requireHW = properties.GetProperty("video.hwaccel.required", 0) != 0;

	// Init framerate
	SetFrameRate(5,300,20);

	// Choix de l'encodeur (VAAPI si demandé/exigé et utilisable, sinon logiciel)
	if (!SelectCodec(tryHW || requireHW))
		return;

	picture = av_frame_alloc();
}

/***********************
* SelectCodec
*	Choisit l'encodeur (VAAPI ou logiciel) et alloue le contexte
************************/
bool FfVideoEncoder::SelectCodec(bool tryHW)
{
	// Libère un éventuel contexte précédent (bascule VAAPI -> logiciel).
	// Ouverture et destruction d'un contexte d'encodage sont sérialisées
	// process-wide : certains backends (libsvtav1 0.9.0) tiennent un état
	// global qu'un init concurrent d'un deinit fait déréférencer à NULL.
	// Voir medkit/ffcodeclock.h pour le détail du défaut.
	if (ctx)
	{
		std::lock_guard<std::mutex> lock(FfCodecOpenLock());
		avcodec_free_context(&ctx);
	}
	codec = NULL;

	if (tryHW && !hwFailed)
	{
		// Cherche un encodeur VAAPI pour ce codec. avcodec_find_encoder()
		// renvoie l'encodeur logiciel (ex libx264) : les encodeurs matériels
		// (h264_vaapi...) doivent être sélectionnés explicitement.
		void *it = NULL;
		const AVCodec *c;
		while (!codec && (c = av_codec_iterate(&it)))
		{
			if (!av_codec_is_encoder(c) || c->id != avCodecId)
				continue;
			for (int i = 0; ; i++)
			{
				const AVCodecHWConfig *hc = avcodec_get_hw_config(c, i);
				if (!hc)
					break;
				if (hc->device_type == AV_HWDEVICE_TYPE_VAAPI)
				{
					codec = c;
					break;
				}
			}
		}

		if (codec)
		{
			// Device VAAPI PARTAGÉ (cf. TryVAAPI) : jamais de device privé, les
			// surfaces doivent circuler entre codecs et graphe de composition.
			AVBufferRef *shared = Pict::GetVAAPIDevice();
			AVBufferRef *dev = shared ? av_buffer_ref(shared) : NULL;
			if (!dev)
			{
				codec = NULL;
				hwFailed = true;
				// Mode HW exigé : aucun repli logiciel autorisé -> échec.
				if (requireHW)
					return Error("FFMpeg encoder: VAAPI device required but unavailable\n");
				Log("FFMpeg encoder: no usable VAAPI device, falling back to software\n");
			}
			else
			{
				ctx = avcodec_alloc_context3(codec);
				ctx->hw_device_ctx = dev;
				Log("FFMpeg encoder: using VAAPI hardware encoder [%s]\n", codec->name);
				return true;
			}
		}
		else
		{
			// Mode HW exigé : pas d'encodeur VAAPI pour ce codec -> échec.
			if (requireHW)
				return Error("FFMpeg encoder: VAAPI encoder required but none for [%s]\n", avcodec_get_name(avCodecId));
			Log("FFMpeg encoder: no VAAPI encoder for [%s], using software\n", avcodec_get_name(avCodecId));
		}
	}

	// Encodeur logiciel par défaut : si un nom est fourni, on privilégie
	// explicitement ce backend (ex : "libsvtav1" plutôt que "libaom-av1",
	// que avcodec_find_encoder() renvoie par défaut pour AV1 mais qui n'est
	// pas temps réel), avec repli sur l'ID si non trouvé.
	if (codecName)
		codec = avcodec_find_encoder_by_name(codecName);
	if (!codec)
		codec = avcodec_find_encoder(avCodecId);
	if (!codec)
		return Error("Encoder [%s] not supported in ffmpeg\n", avcodec_get_name(avCodecId));

	if (codec->type != AVMEDIA_TYPE_VIDEO)
	{
		codec = NULL;
		return Error("FFMpeg encoder [%s] is not a video encoder\n", avcodec_get_name(avCodecId));
	}

	ctx = avcodec_alloc_context3(codec);
	return true;
}

/***********************
* CloseCodec
*	Ferme le codec et repart sur un contexte vierge (même encodeur)
************************/
void FfVideoEncoder::CloseCodec()
{
	if (!ctx)
		return;

	// Conserve le device VAAPI pour le contexte suivant
	AVBufferRef *dev = ctx->hw_device_ctx ? av_buffer_ref(ctx->hw_device_ctx) : NULL;

	if (hw_frame)
		av_frame_free(&hw_frame);

	//Sérialisé : cf. medkit/ffcodeclock.h
	{
		std::lock_guard<std::mutex> lock(FfCodecOpenLock());
		avcodec_free_context(&ctx);
	}
	ctx = avcodec_alloc_context3(codec);
	ctx->hw_device_ctx = dev;

	opened = false;
}

/***********************
* ReopenCodec
*	Réouverture à chaud aux dimensions courantes
************************/
int FfVideoEncoder::ReopenCodec()
{
	int width  = ctx->width;
	int height = ctx->height;

	CloseCodec();

	return SetSize(width, height);
}

/***********************
* FallbackToSoftware
*	Bascule définitive sur l'encodeur logiciel après un échec VAAPI
************************/
int FfVideoEncoder::FallbackToSoftware()
{
	// Mode HW exigé : aucun repli logiciel autorisé.
	if (requireHW)
		return Error("FFMpeg encoder: VAAPI required but open failed; no software fallback\n");

	int width  = ctx->width;
	int height = ctx->height;

	hwFailed = true;

	if (hw_frame)
		av_frame_free(&hw_frame);
	//Sérialisé : cf. medkit/ffcodeclock.h
	{
		std::lock_guard<std::mutex> lock(FfCodecOpenLock());
		avcodec_free_context(&ctx);
	}

	if (!SelectCodec(false))
		return 0;

	return SetSize(width, height);
}

/***********************
* ~FfVideoEncoder
*	Destructor
************************/
FfVideoEncoder::~FfVideoEncoder()
{
	if (frame)
		delete frame;

	if (hw_frame)
		av_frame_free(&hw_frame);

	//Sérialisé : cf. medkit/ffcodeclock.h. C'est le site qui a tué le serveur —
	//l'arrêt de l'encodeur d'une patte pendant l'ouverture de celui de l'autre.
	if (ctx)
	{
		std::lock_guard<std::mutex> lock(FfCodecOpenLock());
		avcodec_free_context(&ctx);
	}

	if (picture)
		av_frame_free(&picture);
}

/***********************
* SetSize
*	Inicializa el tamao de la imagen a codificar
************************/
int FfVideoEncoder::SetSize(int width, int height)
{
	Log("-SetSize [%d,%d]\n",width,height);

	if (!ctx)
		return Error("FfVideoEncoder: no encoder context\n");

	// Redimensionnement à chaud : un contexte ffmpeg ne se rouvre pas, on le
	// remplace par un contexte vierge sur le même encodeur.
	if (opened)
		CloseCodec();

	// Set pixel format
	if (ctx->hw_device_ctx)
	{
		ctx->pix_fmt = AV_PIX_FMT_VAAPI;
		picture->format = AV_PIX_FMT_VAAPI;
	}
	else
	{
		ctx->pix_fmt = AV_PIX_FMT_YUV420P;
		picture->format		= AV_PIX_FMT_YUV420P;
	}
	
	ctx->width 		= width;
	ctx->height 	= height;

	// L'API avcodec_send_frame() exige que l'AVFrame porte lui-même son
	// format et ses dimensions (l'ancienne API les déduisait du contexte).
	
	picture->width		= width;
	picture->height		= height;

	// Set picture data
	picture->linesize[0] = width;
	picture->linesize[1] = width/2;
	picture->linesize[2] = width/2;

	// Open codec
	return OpenCodec();
}

/*************************
* SetFrameRate
* 	Indica el numero de frames por segudno actual
***************************/
int FfVideoEncoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Save frame rate
	if (frames>0)
		fps=frames;
	else
		fps=10;

	// Save bitrate
	if (kbits>0)
		bitrate=kbits*1024;

	//Save intra period
	if (intraPeriod>0)
		this->intraPeriod = intraPeriod;

	return 1;
}

/***********************
* OpenCodec
*	Abre el codec
************************/
int FfVideoEncoder::OpenCodec()
{
	Log("-OpenCodec %s [%s,%dbps,%dfps]\n", VideoCodec::GetNameFor(type), codec ? codec->name : "none", bitrate, fps);

	// Check
	if (codec==NULL)
		return Error("No codec\n");

	// Check
	if (opened)
		return Error("Already opened\n");

	//If already got a buffer
	if (frame)
		//Free it
		delete(frame);

	//Set new buffer size
	DWORD bufSize = 1.5*bitrate/fps;

	//Check size
	if (bufSize<AV_INPUT_BUFFER_MIN_SIZE)
		//Set minimun
		bufSize = AV_INPUT_BUFFER_MIN_SIZE;

	//Y alocamos el buffer
	frame = new VideoFrame(type,bufSize);

	// Bitrate,fps
	ctx->bit_rate 		= bitrate;
	ctx->time_base          = (AVRational){1,fps};
	ctx->gop_size		= intraPeriod;
	ctx->max_b_frames	= 0;

	// Réglages spécifiques au codec (rate control, profil, options privées)
	ConfigureContext();

	if (ctx->hw_device_ctx)
	{
		AllocateVAAPIFrame(ctx);

		if (ctx->hw_device_ctx)
		{
			hw_frame = av_frame_alloc();
			if (!hw_frame) {
				Error("Failed to allocate VAAPI frame. Falling back to CPU.\n");
				av_buffer_unref(&ctx->hw_frames_ctx);
				av_buffer_unref(&ctx->hw_device_ctx);
			}
			else
			{
				hw_frame->format = AV_PIX_FMT_VAAPI;
				hw_frame->width = ctx->width;
				hw_frame->height = ctx->height;
			}
		}

		// L'init VAAPI (AllocateVAAPIFrame ou l'allocation de hw_frame) a
		// échoué : l'encodeur matériel ne sait pas consommer de trames
		// système, bascule complète sur l'encodeur logiciel.
		if (!ctx->hw_device_ctx)
			return FallbackToSoftware();
	}

	// Open codec. Sérialisé : cf. medkit/ffcodeclock.h. Le verrou couvre
	// STRICTEMENT avcodec_open2 — surtout pas FallbackToSoftware() plus bas,
	// qui détruit un contexte et reprendrait donc le même verrou.
	int openErr;
	{
		std::lock_guard<std::mutex> lock(FfCodecOpenLock());
		openErr = avcodec_open2(ctx, codec, NULL);
	}

	if (openErr<0)
	{
		// Un encodeur VAAPI peut refuser à l'ouverture (profil/résolution non
		// supportés par le driver) : on retombe sur l'encodeur logiciel.
		if (IsHWAccelerated())
		{
			Error("ffmpeg is unable to open VAAPI encoder %s, falling back to software\n", codec->name);
			return FallbackToSoftware();
		}
		return Error("ffmpeg is unable to open %s encoder\n", codec->name);
	}

	// We are opened
	opened=true;

	// Référence de la politique de réouverture (ShouldReopenForBitrate)
	openedBitrate = bitrate;

	// Exit
	return 1;
}

bool FfVideoEncoder::ShouldReopenForBitrate() const
{
	if (!opened || openedBitrate <= 0)
		return false;

	// Baisse : un pas de l'AIMD vaut -15 %, il doit passer.
	if (bitrate * 10 <= openedBitrate * 9)
		return true;

	// Hausse : paliers de 1,5x. La boucle d'adaptation monte de +8 %/s, donc un
	// seuil de 10 % vaut une trame clé toutes les 1,3 s — mesuré le 2026-08-20 :
	// 136 réouvertures en 5,5 min, dont deux à 0,16 s d'intervalle.
	return bitrate * 2 >= openedBitrate * 3;
}

/***********************
* ConfigureContext
*	Réglages par défaut des encodeurs logiciels ffmpeg (H263, MPEG4, FLV1...) :
*	quantizer fixe historique. Redéfini par les codecs à rate control propre
*	(H264 : CRF+VBV libx264 ou VBR VAAPI).
************************/
void FfVideoEncoder::ConfigureContext()
{
	ctx->bit_rate_tolerance = bitrate/fps+1;

	// Encoder quality
	ctx->rc_max_rate	= bitrate;
	ctx->rc_buffer_size	= bitrate/fps+1;
	ctx->rc_initial_buffer_occupancy = 0;
	// Tell the encoder to use fixed quantizer (qscale)
	ctx->flags |= AV_CODEC_FLAG_QSCALE;
	// Set the global quality based on a quantizer value of 10
	ctx->global_quality = FF_QP2LAMBDA * 10;
}


/***********************
* EncodeFrame
*	Codifica un frame
************************/
VideoFrame* FfVideoEncoder::EncodeFrame(PictPtr pic)
{
	int ret = 0;
	//Check if we are opened
	if (!opened)
		return NULL;

	if (!pic || !pic->GetAVFrame())
		return NULL;

	AVFrame* src = pic->GetAVFrame();

	// L'encodeur est configuré pour ctx->width x ctx->height (SetSize) : on refuse
	// une trame de dimensions différentes (le pipeline amont gère le resize).
	if (src->width != ctx->width || src->height != ctx->height)
	{
		Error("-EncodeFrame: frame size %dx%d != encoder %dx%d\n",
		      src->width, src->height, ctx->width, ctx->height);
		return NULL;
	}

	const bool encoderHW = (ctx->hw_device_ctx && ctx->hw_frames_ctx && hw_frame);
	const bool srcHW     = (src->format == AV_PIX_FMT_VAAPI);

	AVPacket* pkt = av_packet_alloc();

	// Trame effectivement envoyée à l'encodeur. On ne mute JAMAIS `src` (trame
	// partagée/immuable) : on travaille sur une trame que l'encodeur possède
	// (hw_frame pour l'upload, ou `picture` en simple référence des buffers de src).
	AVFrame* frameToSend = NULL;
	PictPtr  cpuTmp;   // garde en vie une éventuelle redescente GPU->CPU

	if (encoderHW && !srcHW)
	{
		// CPU -> GPU : upload dans une surface VAAPI neuve du pool.
		av_frame_unref(hw_frame);
		hw_frame->format = AV_PIX_FMT_VAAPI;
		hw_frame->width  = ctx->width;
		hw_frame->height = ctx->height;

		ret = av_hwframe_get_buffer(ctx->hw_frames_ctx, hw_frame, 0);
		if (ret < 0) {
			Error("Failed to get VAAPI surface: %s\n", AVErrToStr(ret));
			av_packet_free(&pkt);
			return NULL;
		}

		ret = av_hwframe_transfer_data(hw_frame, src, 0);
		if (ret < 0) {
			Error("Failed to transfer frame to VAAPI: %s\n", AVErrToStr(ret));
			av_packet_free(&pkt);
			return NULL;
		}
		frameToSend = hw_frame;
	}
	else
	{
		// Cas envoyés par simple référence (buffers partagés avec src, zéro-copie) :
		//  - SW + src CPU        : envoi direct
		//  - HW + src déjà GPU   : envoi direct de la surface VAAPI
		//  - SW + src GPU        : redescente explicite puis envoi
		const AVFrame* refSrc = src;
		if (!encoderHW && srcHW)
		{
			// GPU -> SW : l'encodeur logiciel a besoin d'une trame CPU.
			cpuTmp = pic->DownloadToCPU();
			if (!cpuTmp || !cpuTmp->GetAVFrame()) {
				Error("-EncodeFrame: GPU->CPU download failed\n");
				av_packet_free(&pkt);
				return NULL;
			}
			refSrc = cpuTmp->GetAVFrame();
		}

		av_frame_unref(picture);
		ret = av_frame_ref(picture, refSrc);
		if (ret < 0) {
			Error("-EncodeFrame: av_frame_ref failed: %s\n", AVErrToStr(ret));
			av_packet_free(&pkt);
			return NULL;
		}
		frameToSend = picture;
	}

	// pts monotone requis par les encodeurs ffmpeg (libx264 notamment), en unités
	// de time_base (1/fps). Champs de conteneur : ne touchent pas les buffers de src.
	frameToSend->pts = pts++;

	// Demande d'intra (FPU) : forcée ici, sur la trame envoyée uniquement.
	if (forceIntra)
	{
		frameToSend->key_frame = 1;
		frameToSend->pict_type = AV_PICTURE_TYPE_I;
		forceIntra = false;
	}
	else
	{
		frameToSend->key_frame = 0;
		frameToSend->pict_type = AV_PICTURE_TYPE_NONE;
	}

	bool firstPacket = true;

	ret = avcodec_send_frame(ctx, frameToSend);
	if (ret < 0) {
		Error("Encoding error: %s\n", AVErrToStr(ret));
		av_packet_free(&pkt);
		return NULL;
	}

	DWORD size = 0;

	do {
		ret = avcodec_receive_packet(ctx, pkt);
		if (ret >= 0)
		{
			if (firstPacket) {
				//Set width and height
				frame->SetWidth(ctx->width);
				frame->SetHeight(ctx->height);

				//Is intra
				frame->SetIntra( (pkt->flags & AV_PKT_FLAG_KEY) != 0 );

				//Clean all previous packets
				frame->ClearRTPPacketizationInfo();

				//Reset previous content
				frame->SetLength(0);

				firstPacket = false;
			}

			// Recopie la trame binaire encodée dans le tampon de la VideoFrame.
			// Avec l'API send/receive, ffmpeg écrit dans SON propre tampon
			// (pkt->data) ; la packetisation RTP (PacketizeFrame) référence des
			// offsets dans le tampon de la frame, qu'il faut donc remplir.
			// Accumulation : un même envoi peut produire plusieurs paquets.
			frame->AppendMedia(pkt->data, pkt->size);
			size += pkt->size;
		}
		else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			// Plus de paquet disponible pour cette trame : fin normale du drain.
			// (NE PAS retourner NULL ici : la trame déjà packetisée serait perdue.)
			break;
		}
		else
		{
			Error("Encoding error (receive_packet): %s\n", AVErrToStr(ret));
			av_packet_free(&pkt);
			return NULL;
		}
		av_packet_unref(pkt);
	} while(ret >= 0);

	av_packet_free(&pkt);

	// Aucune donnée produite (trame éventuellement bufferisée par l'encodeur).
	if (size == 0)
		return NULL;

	//Set length
	frame->SetLength(size);

	// Packetisation RTP propre au codec (virtuelle).
	PacketizeFrame();

	return frame;
}

/***********************
* PacketizeFrame (défaut)
*	Schéma historique H263/MPEG4 : saute le start code (2 octets) et fragmente
*	avec un préfixe de payload RFC 2429. Redéfini par les codecs à packetisation
*	RTP spécifique (cf. VP8Encoder::PacketizeFrame).
************************/
void FfVideoEncoder::PacketizeFrame()
{
	DWORD len = frame->GetLength();
	DWORD ini = 2;		// saute le start code 2 octets
	BYTE prefix[2] = { 0x04, 0x00 };

	while (ini < len)
	{
		bool mark = false;
		DWORD lenpkt = RTPPAYLOADSIZE-2;
		if (lenpkt+ini >= len)
		{
			mark = true;
			lenpkt = len-ini;
		}

		frame->AddRtpPacket(ini, lenpkt, prefix, 2, mark);

		// Paquets suivants : préfixe sans le bit P.
		if (ini == 2)
		{
			prefix[0] = 0x00;
			prefix[1] = 0x00;
		}

		ini += lenpkt;
	}
}

/***********************
* FastPictureUpdate
*	Manda un frame entero
************************/
int FfVideoEncoder::FastPictureUpdate()
{
	// Forçage appliqué au prochain EncodeFrame, sur la trame réellement envoyée
	// (le membre `picture` est désormais une simple référence de la trame source).
	forceIntra = true;
	return 1;
}

/***********************
* FfVideoDecoder
*	Consturctor
************************/
bool FfVideoDecoder::IsCodecAvailable(enum AVCodecID id, const char* preferredName)
{
	// Même logique de résolution que le constructeur (nom préféré puis ID),
	// sans ouvrir le codec.
	if (preferredName && avcodec_find_decoder_by_name(preferredName))
		return true;
	return avcodec_find_decoder(id) != nullptr;
}

FfVideoDecoder::FfVideoDecoder(enum AVCodecID av_codec, enum VideoCodec::Type codec_id, const char* codec_name,
                               bool requireHW)
{
	//Guardamos los valores por defecto
	type = codec_id;
	bufLen = 0;
	this->requireHW = requireHW;
	// Initialisés tôt : un return anticipé (codec introuvable, HW exigé absent)
	// laisserait sinon le destructeur free() des pointeurs indéterminés.
	ctx = NULL; parser_ctx = NULL; picture = NULL;
	buffer = NULL; src = 0;

	//Encotramos el codec : si un nom est fourni, on le privilégie (explicite
	// plutôt que de dépendre de l'ordre de résolution par défaut de ffmpeg),
	// avec repli sur l'ID si non trouvé.
	codec = NULL;
	if (codec_name)
		codec = avcodec_find_decoder_by_name(codec_name);
	if (!codec)
		codec = avcodec_find_decoder(av_codec);

	//Comprobamos
	if(codec==NULL)
	{
		Error("No decoder found\n");
		return ;
	}

	//Alocamos el contxto y el picture
	ctx = avcodec_alloc_context3(codec);
	parser_ctx = av_parser_init(ctx->codec_id);

	if (!parser_ctx) {
        Error("Unable to open FFmpeg decoder (parser). Codec ID = %s", VideoCodec::GetNameFor(type));
    }
	// `picture` (PictPtr) est assigné à chaque Decode() ; pas de pré-allocation ici.

	bool hwOk = TryVAAPI(ctx, codec);
	if (ctx->hw_device_ctx)
		// Sans ce callback le décodeur ne négocie jamais le format matériel
		// et reste en logiciel malgré hw_device_ctx.
		ctx->get_format = GetVAAPIFormat;

	// Mode HW exigé : sans device VAAPI on refuse d'ouvrir le décodeur (pas de
	// repli logiciel). IsHardwareReady() restera false et Decode() échouera.
	if (requireHW && !hwOk)
	{
		Error("FFMpeg decoder [%s]: VAAPI device required but unavailable\n", VideoCodec::GetNameFor(type));
		avcodec_free_context(&ctx);   // ctx==NULL -> IsHardwareReady()=false, Decode() sort en erreur
		return;
	}

	//POnemos los valores del contexto
	ctx->workaround_bugs 	= 255*255;
	ctx->error_concealment 	= FF_EC_GUESS_MVS | FF_EC_DEBLOCK;

	//Alocamos el buffer
	bufSize = 1024*756*3/2;
	buffer = (BYTE *)malloc(bufSize);
	src = 0;

	//Lo abrimos
	avcodec_open2(ctx, codec, NULL);
}

/***********************
* ~FfVideoDecoder
*	Destructor
************************/
FfVideoDecoder::~FfVideoDecoder()
{
	free(buffer);
	if (parser_ctx)
		av_parser_close(parser_ctx);
	// `picture` est un PictPtr : sa destruction (shared_ptr) libère l'AVFrame.
	avcodec_free_context(&ctx);	// ferme aussi le codec
}

int FfVideoDecoder::Decode(BYTE *buffer,DWORD size)
{
	// Contexte absent (décodeur non ouvert, ex. HW VAAPI exigé mais indisponible).
	if (!ctx)
		return Error("[%s] decoder not opened (no context)\n", VideoCodec::GetNameFor(type));

	//Decodificamos
	AVPacket* pkt = av_packet_alloc();
	BYTE *current_ptr = buffer;
    int remaining_size = size;
	int ret;
	// Chaque trame décodée est reçue dans son PROPRE AVFrame (un Pict neuf) :
	// c'est ce qui évite le piège du picture réutilisé par avcodec_receive_frame.
	// On ne remplace le membre `picture` que si une trame a réellement été reçue.
	PictPtr pict;

	while (remaining_size > 0) {
		if (parser_ctx) {
			int bytes_parsed = av_parser_parse2(
	            parser_ctx, ctx,
	            &pkt->data, &pkt->size,
	            current_ptr, remaining_size,
	            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0
	        );

			if (bytes_parsed < 0) {
	            Error("Error from %s parser. Error = %d\n", VideoCodec::GetNameFor(type), bytes_parsed);
	            goto error;
	        }

			current_ptr += bytes_parsed;
	        remaining_size -= bytes_parsed;
		} else {
			// Pas de parser pour ce codec (ex. H263+) : l'entrée est déjà une
			// trame complète, on la pousse telle quelle.
			pkt->data = current_ptr;
			pkt->size = remaining_size;
			remaining_size = 0;
		}

		if (pkt->size > 0) {
			ret = avcodec_send_packet(ctx, pkt);

			if (ret < 0) {
				Error("%s decoding error (send packet). Error = %s\n", VideoCodec::GetNameFor(type), AVErrToStr(ret));
                goto error;
            }

			PictPtr tmp = std::make_shared<Pict>(av_frame_alloc());
			ret = avcodec_receive_frame(ctx, tmp->GetAVFrame());
			if (ret == 0) {
				// Trame prête : on la garde.
				pict = tmp;
			} else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				// Pas de frame prête pour ce paquet (ex: NAL SPS/PPS seul) : normal.
			} else {
				Error("%s decoding error (receive frame). Error = %s\n", VideoCodec::GetNameFor(type), AVErrToStr(ret));
				goto error;
			}
		}
	}

	if (pkt) av_packet_free(&pkt);
	// Ne remplace le membre que si on a décodé une trame complète.
	if (pict) picture = pict;
	return 1;
error:
	if (pkt) av_packet_free(&pkt);
	return 0;
}

/***********************
* DecodePacket (défaut)
*	Dépaquetisation RTP générique : accumule le payload brut dans le tampon
*	membre et décode la trame complète sur 'last'. Convient aux codecs sans
*	en-tête de payload RTP à retirer (MPEG4, VP6, FLV1/SORENSON...).
*	Les codecs à dépaquetisation spécifique (H264, H263+, VP8) redéfinissent
*	cette méthode dans leur propre classe dérivée.
************************/
int FfVideoDecoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	// Vérifie la place disponible (+ padding ffmpeg)
	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE > bufSize)
	{
		Log("-DecodePacket buffer size error, reseting\n");
		bufLen = 0;
		return 0;
	}

	// Payload déjà exploitable tel quel : accumulation brute.
	memcpy(buffer+bufLen,in,inLen);
	bufLen += inLen;

	// Dernier paquet de la trame : on décode.
	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		ret = Decode(buffer,bufLen);
		bufLen = 0;
	}

	return ret;
}

PictPtr FfVideoDecoder::GetFrame()
{
	if (!picture)
		return nullptr;

	AVFrame *pic = picture->GetAVFrame();
	if (!pic || pic->width==0 || pic->height==0) {
		Error("-Wrong dimensions [%d,%d]\n", pic?pic->width:0, pic?pic->height:0);
		return nullptr;
	}

	// PAS de redescente GPU->CPU implicite : on renvoie la trame telle quelle
	// (surface VAAPI ou CPU), partage zéro-copie du PictPtr. C'est au consommateur
	// de décider via Pict::IsGPUPict() / Pict::DownloadToCPU() (cf. avframe.md),
	// afin de préserver un éventuel pipeline GPU de bout en bout.
	return picture;
}
