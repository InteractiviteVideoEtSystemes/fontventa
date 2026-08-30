#include "ffaudiocodec.h"
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}
#include "medkit/log.h"
#include <string.h>


bool MapAudioCodec( enum AVCodecID id, AudioCodec::Type & out )
{
    switch( id )
    {
        case AV_CODEC_ID_PCM_MULAW: out = AudioCodec::PCMU; return true;
        case AV_CODEC_ID_PCM_ALAW:  out = AudioCodec::PCMA; return true;
        case AV_CODEC_ID_AMR_NB:    out = AudioCodec::AMR;  return true;
        case AV_CODEC_ID_AMR_WB:    out = AudioCodec::AMRWB;return true;
        case AV_CODEC_ID_OPUS:      out = AudioCodec::OPUS; return true;
        case AV_CODEC_ID_ADPCM_G722:out = AudioCodec::G722; return true;
        case AV_CODEC_ID_GSM:
        case AV_CODEC_ID_GSM_MS:    out = AudioCodec::GSM;  return true;
        case AV_CODEC_ID_AAC:       out = AudioCodec::AAC;  return true;
        default:                                            return false;
    }
}


bool FfAudioEncoder::IsCodecAvailable(enum AVCodecID id, const char* preferredName)
{
	// Meme logique de resolution que le constructeur : nom prefere d'abord, puis
	// repli sur l'ID. Ne rien ouvrir (avcodec_open2), juste tester la presence.
	if (preferredName && avcodec_find_encoder_by_name(preferredName))
		return true;
	return avcodec_find_encoder(id) != nullptr;
}

FfAudioEncoder::FfAudioEncoder(const Properties& properties, enum AVCodecID av_codec, AudioCodec::Type codec_id,
                               const char* codec_name) :
	defaultSampleRate(8000), codec(nullptr), ctx(nullptr), swr(nullptr), fifo(nullptr),
	frame(nullptr), pkt(nullptr), opened(false), allocatedSamples(0),
	nextPts(AV_NOPTS_VALUE)
{
	// Membres hérités d'AudioEncoder
	type = codec_id;
	numFrameSamples = 0;

	// Recherche de l'encodeur : si un nom est fourni, on privilégie le codec natif
	// (avcodec_find_encoder_by_name) plutôt que le wrapper externe que
	// avcodec_find_encoder() peut retourner en premier. Repli sur l'ID si non trouvé.
	if (codec_name)
		codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec)
		codec = avcodec_find_encoder(av_codec);
	if (!codec)
	{
		Error("Encoder [%s] not supported in ffmpeg\n", avcodec_get_name(av_codec));
		return;
	}

	if (codec->type != AVMEDIA_TYPE_AUDIO)
	{
		Error("FFmpeg encoder [%s] is not an audio encoder\n", codec->name);
		return;
	}

	// Allocation du contexte ffmpeg
	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
	{
		Error("Could not allocate context for encoder [%s]\n", codec->name);
		return;
	}

	// Mono (API channel-layout ffmpeg >= 5.1)
	av_channel_layout_default(&ctx->ch_layout, 1);

	// La classe dérivée règle ensuite bitrate/options puis appelle
	// TrySetRate() et Open().
}

FfAudioEncoder::~FfAudioEncoder()
{
	if (swr)	swr_free(&swr);
	if (fifo)	av_audio_fifo_free(fifo);
	if (frame)	av_frame_free(&frame);
	if (pkt)	av_packet_free(&pkt);
	if (ctx)	avcodec_free_context(&ctx);	// libère aussi ch_layout
}

DWORD FfAudioEncoder::GetRate()
{
	return (ctx && ctx->sample_rate) ? (DWORD)ctx->sample_rate : 0;
}

bool FfAudioEncoder::IsSigned16FmtSupported() const
{
	// sample_fmts == NULL : aucune contrainte sur le format.
	if (!codec->sample_fmts)
		return true;

	for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++)
		if (codec->sample_fmts[i] == AV_SAMPLE_FMT_S16)
			return true;

	return false;
}

bool FfAudioEncoder::IsRateNativelySupported(DWORD rate) const
{
	// supported_samplerates == NULL : toutes les fréquences sont acceptées.
	if (!codec->supported_samplerates)
		return true;

	for (int i = 0; codec->supported_samplerates[i] != 0; i++)
		if ((DWORD)codec->supported_samplerates[i] == rate)
			return true;

	return false;
}

DWORD FfAudioEncoder::TrySetRate(DWORD rate)
{
	if (!ctx)
	{
		Error("[%s] codec not initialised\n", codec ? codec->name : "?");
		return defaultSampleRate;
	}

	// Un encodeur déjà ouvert ne peut plus être reconfiguré : ffmpeg fige le
	// contexte à avcodec_open2, et réécrire ctx->sample_rate serait sans effet.
	// Certains encodeurs (AMR-NB/WB) s'ouvrent dès leur constructeur ; leur
	// fréquence de travail est alors figée à sa valeur native (8000/16000 Hz).
	// On renvoie cette fréquence RÉELLE plutôt qu'un taux qui serait ignoré —
	// sans quoi le pipeline (OpenAudioTranscoded) calcule une taille de trame
	// erronée (cf. IsRateNativelySupported qui, faute de supported_samplerates
	// déclaré par libopencore, croit toutes les fréquences acceptées).
	if (opened)
		return ctx->sample_rate;

	// Mémorise le taux d'entrée (taux pipeline du MCU) avant tout ajustement.
	inputRate = rate;

	const bool s16ok  = IsSigned16FmtSupported();
	const bool rateok = IsRateNativelySupported(rate);

	// L'entrée du mediaserver est toujours du S16 mono. On encode en S16 si
	// le codec l'accepte, sinon dans son premier format natif via un resampler.
	ctx->sample_fmt  = s16ok  ? AV_SAMPLE_FMT_S16 : codec->sample_fmts[0];
	ctx->sample_rate = rateok ? (int)rate : (int)defaultSampleRate;

	if (!s16ok || !rateok)
		Log("[%s]: S16/%u Hz non supporté nativement -> resampler vers %s/%d Hz\n",
			codec->name, rate, av_get_sample_fmt_name(ctx->sample_fmt), ctx->sample_rate);

	if (!SetupResampler(rate))
		return defaultSampleRate;

	return ctx->sample_rate;
}

bool FfAudioEncoder::SetupResampler(DWORD rate)
{
	// On repart d'un resampler propre à chaque (re)configuration.
	if (swr)
		swr_free(&swr);

	if (rate == 0)
		return false;

	// Entrée déjà au format et à la fréquence du codec : chemin direct, swr nul.
	if (ctx->sample_fmt == AV_SAMPLE_FMT_S16 && (int)rate == ctx->sample_rate)
		return true;

	AVChannelLayout mono;
	av_channel_layout_default(&mono, 1);

	int err = swr_alloc_set_opts2(&swr,
		&mono, ctx->sample_fmt,   ctx->sample_rate,	// sortie
		&mono, AV_SAMPLE_FMT_S16, (int)rate,		// entrée
		0, nullptr);

	const bool ok = (err >= 0 && swr_init(swr) >= 0);
	if (!ok)
	{
		Error("[%s] failed to configure resampler %u Hz -> %d Hz\n",
			codec->name, rate, ctx->sample_rate);
		if (swr)
			swr_free(&swr);
	}

	av_channel_layout_uninit(&mono);
	return ok;
}

bool FfAudioEncoder::Open()
{
	if (opened)
		return true;

	if (!ctx)
	{
		Error("[%s] no context to open\n", codec ? codec->name : "?");
		return false;
	}

	if (avcodec_open2(ctx, codec, nullptr) < 0)
	{
		Error("[%s] could not open encoder\n", codec->name);
		return false;
	}

	// frame_size n'est connu qu'après ouverture. 0 = le codec accepte n'importe
	// quelle taille (PCM, G.711...) : on impose alors 20 ms, la granularité que
	// le pipeline RTP attend, pour que numFrameSamples soit TOUJOURS exploitable.
	numFrameSamples = ctx->frame_size;
	if (numFrameSamples <= 0)
		numFrameSamples = ctx->sample_rate / 50;
	if (numFrameSamples <= 0)
		numFrameSamples = 160;

	frame = av_frame_alloc();
	pkt   = av_packet_alloc();
	if (!frame || !pkt)
	{
		Error("[%s] could not allocate frame/packet\n", codec->name);
		return false;
	}

	// Fifo d'accumulation au format/fréquence du codec : c'est elle qui permet
	// d'accepter des trames d'entrée de n'importe quelle taille. Elle grandit
	// toute seule ; MaxFifoSamples la borne en cas de consommateur en retard.
	fifo = av_audio_fifo_alloc(ctx->sample_fmt, ctx->ch_layout.nb_channels, numFrameSamples * 4);
	if (!fifo)
	{
		Error("[%s] could not allocate sample fifo\n", codec->name);
		return false;
	}

	// Le tampon de la trame d'entrée est alloué paresseusement par EnsureFrame()
	// au premier EncodeFrame() : ainsi les codecs à frame_size variable
	// (frame_size==0) sont gérés comme ceux à trame fixe.

	opened = true;
	Log("[%s] encoder open: frame size %d, %d Hz, fmt %s\n",
		codec->name, numFrameSamples, ctx->sample_rate, av_get_sample_fmt_name(ctx->sample_fmt));
	return true;
}

bool FfAudioEncoder::EnsureFrame(int nb)
{
	// (Ré)alloue le tampon de la trame d'entrée pour au moins `nb` échantillons
	// dans le format/layout du codec. Réutilisé tant qu'il est assez grand.
	if (!frame->data[0] || allocatedSamples < nb || frame->format != ctx->sample_fmt)
	{
		av_frame_unref(frame);
		frame->format      = ctx->sample_fmt;
		frame->sample_rate = ctx->sample_rate;
		av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
		frame->nb_samples  = nb;
		if (av_frame_get_buffer(frame, 0) < 0)
		{
			allocatedSamples = 0;
			return false;
		}
		allocatedSamples = nb;
	}

	// Restaure la pleine capacité allouée (un appel précédent a pu réduire
	// nb_samples au nombre réellement produit).
	frame->nb_samples = allocatedSamples;
	return true;
}

void FfAudioEncoder::DrainResampler()
{
	if (!swr)
		return;

	const int pending = (int)swr_get_delay(swr, ctx->sample_rate);
	if (pending <= 0)
		return;

	uint8_t **conv = nullptr;
	if (av_samples_alloc_array_and_samples(&conv, nullptr, ctx->ch_layout.nb_channels,
			pending, ctx->sample_fmt, 0) < 0)
		return;

	const int got = swr_convert(swr, conv, pending, nullptr, 0);
	if (got > 0)
		av_audio_fifo_write(fifo, (void**)conv, got);

	av_freep(&conv[0]);
	av_freep(&conv);
}

bool FfAudioEncoder::PushToFifo(SamplesPtr samples)
{
	AVFrame *src = samples ? samples->GetAVFrame() : nullptr;
	if (!src || src->nb_samples <= 0)
		return false;

	// LA TRAME FAIT FOI : si sa fréquence a changé, on reconfigure le
	// rééchantillonneur ici, sans état partagé avec le producteur. C'est ce qui
	// rend impossible le bug de fréquence périmée.
	DWORD rate = samples->GetRate();
	if (rate == 0)
	{
		Error("[%s] frame without sample rate\n", codec->name);
		return false;
	}

	if (rate != inputRate)
	{
		Log("[%s] fréquence d'entrée %u -> %u Hz, resampler reconfiguré\n",
			codec->name, inputRate, rate);
		// Vider l'ancien resampler AVANT de le remplacer : ce qu'il retient
		// appartient au flux, le jeter ferait un trou audible à chaque bascule.
		DrainResampler();
		inputRate = rate;
		if (!SetupResampler(rate))
			return false;
	}

	// Horodatage : la première trame donne l'origine, la fifo fait le reste.
	if (nextPts == AV_NOPTS_VALUE && src->pts != AV_NOPTS_VALUE)
		nextPts = av_rescale(src->pts, ctx->sample_rate, (int64_t)rate);

	if (!swr)
	{
		// Format et fréquence identiques : écriture directe dans la fifo.
		if (av_audio_fifo_write(fifo, (void**)src->data, src->nb_samples) < src->nb_samples)
		{
			Error("[%s] fifo write failed\n", codec->name);
			return false;
		}
	}
	else
	{
		// Capacité de sortie : ce qui reste en attente dans swr + la nouvelle
		// entrée, converti à la fréquence du codec, arrondi au supérieur.
		const int cap = (int)av_rescale_rnd(
			swr_get_delay(swr, (int64_t)rate) + src->nb_samples,
			ctx->sample_rate, (int64_t)rate, AV_ROUND_UP);

		uint8_t **conv = nullptr;
		if (av_samples_alloc_array_and_samples(&conv, nullptr, ctx->ch_layout.nb_channels,
				cap, ctx->sample_fmt, 0) < 0)
		{
			Error("[%s] could not allocate resampling buffer\n", codec->name);
			return false;
		}

		const int got = swr_convert(swr, conv, cap,
			(const uint8_t**)src->data, src->nb_samples);

		bool ok = true;
		if (got > 0 && av_audio_fifo_write(fifo, (void**)conv, got) < got)
		{
			Error("[%s] fifo write failed\n", codec->name);
			ok = false;
		}

		av_freep(&conv[0]);
		av_freep(&conv);

		if (!ok)
			return false;
	}

	// Garde-fou : un consommateur en retard ne doit pas faire enfler la fifo
	// indéfiniment. Au-delà d'une seconde, on jette le plus ancien.
	const int excess = av_audio_fifo_size(fifo) - ctx->sample_rate;
	if (excess > 0)
	{
		Error("[%s] fifo overflow, dropping %d samples\n", codec->name, excess);
		av_audio_fifo_drain(fifo, excess);
	}

	return true;
}

AudioFramePtr FfAudioEncoder::EncodeFromFifo()
{
	// Un encodeur peut retenir ses premières trames (délai d'amorçage, AAC) :
	// on continue de le nourrir tant que la fifo le permet, jusqu'à obtenir un
	// paquet. Rien à rendre quand la fifo est trop courte.
	while (av_audio_fifo_size(fifo) >= numFrameSamples)
	{
		if (!EnsureFrame(numFrameSamples))
		{
			Error("[%s] could not allocate frame buffer\n", codec->name);
			return nullptr;
		}

		if (av_frame_make_writable(frame) < 0)
		{
			Error("[%s] frame not writable\n", codec->name);
			return nullptr;
		}

		if (av_audio_fifo_read(fifo, (void**)frame->data, numFrameSamples) < numFrameSamples)
		{
			Error("[%s] fifo read failed\n", codec->name);
			return nullptr;
		}

		frame->nb_samples = numFrameSamples;
		frame->pts        = nextPts;

		int ret = avcodec_send_frame(ctx, frame);
		if (ret < 0)
		{
			Error("[%s] avcodec_send_frame: %d\n", codec->name, ret);
			return nullptr;
		}

		AudioFramePtr out = std::make_shared<AudioFrame>(type, GetClockRate());
		DWORD total = 0;

		while ((ret = avcodec_receive_packet(ctx, pkt)) >= 0)
		{
			out->AppendMedia(pkt->data, pkt->size);
			total += pkt->size;
			av_packet_unref(pkt);
		}

		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
		{
			Error("[%s] avcodec_receive_packet: %d\n", codec->name, ret);
			return nullptr;
		}

		// Horodatage de la trame émise, dans l'horloge RTP du codec.
		if (nextPts != AV_NOPTS_VALUE)
		{
			out->SetTimestamp((DWORD)av_rescale(nextPts, GetClockRate(), ctx->sample_rate));
			nextPts += numFrameSamples;
		}
		out->SetDuration((DWORD)((QWORD)numFrameSamples * GetClockRate() / ctx->sample_rate));

		if (total > 0)
			return out;
	}

	return nullptr;
}

AudioFramePtr FfAudioEncoder::EncodeFrame(SamplesPtr samples)
{
	if (!opened && !Open())
	{
		Error("[%s] encoder not opened\n", codec ? codec->name : "?");
		return nullptr;
	}

	if (samples && !PushToFifo(samples))
		return nullptr;

	return EncodeFromFifo();
}

/******************************************************************************
 *                              FfAudioDecoder                                *
 ******************************************************************************/

bool FfAudioDecoder::IsCodecAvailable(enum AVCodecID id, const char* preferredName)
{
	// Même logique de résolution que le constructeur : nom préféré d'abord, puis
	// repli sur l'ID. Ne rien ouvrir (avcodec_open2), juste tester la présence.
	if (preferredName && avcodec_find_decoder_by_name(preferredName))
		return true;
	return avcodec_find_decoder(id) != nullptr;
}

FfAudioDecoder::FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id, const char* codec_name) :
	FfAudioDecoder(av_codec, codec_id, nullptr, 0, codec_name)
{
}

FfAudioDecoder::FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id,
                               const uint8_t* extradata, int extradata_size, const char* codec_name,
                               int sample_rate) :
	codec(nullptr), ctx(nullptr), swr(nullptr), frame(nullptr), pkt(nullptr), opened(false)
{
	// Membres hérités d'AudioDecoder
	type = codec_id;
	numFrameSamples = 0;

	// Recherche du décodeur : si un nom est fourni, on privilégie le décodeur
	// natif (ou wrapper) explicitement demandé plutôt que celui qu'
	// avcodec_find_decoder() retournerait par défaut, avec repli sur l'ID si non
	// trouvé (même logique que FfAudioEncoder).
	if (codec_name)
		codec = avcodec_find_decoder_by_name(codec_name);
	if (!codec)
		codec = avcodec_find_decoder(av_codec);
	if (!codec)
	{
		Error("Decoder [%s] not supported in ffmpeg\n", avcodec_get_name(av_codec));
		return;
	}

	if (codec->type != AVMEDIA_TYPE_AUDIO)
	{
		Error("FFmpeg decoder [%s] is not an audio decoder\n", codec->name);
		return;
	}

	ctx = avcodec_alloc_context3(codec);
	if (!ctx)
	{
		Error("Could not allocate context for decoder [%s]\n", codec->name);
		return;
	}

	// Mono, et on demande du S16 en sortie (les décodeurs qui savent le faire
	// l'honorent ; sinon on convertit via le resampler).
	av_channel_layout_default(&ctx->ch_layout, 1);
	ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;

	// Certains décodeurs sans extradata exigent la fréquence dès l'ouverture
	// (le speex natif la refuse hors {8000, 16000, 32000} et échoue sur 0).
	if (sample_rate > 0)
		ctx->sample_rate = sample_rate;

	// Extradata (AudioSpecificConfig pour l'AAC des MP4) : recopié avec le
	// padding ffmpeg requis. Sans lui, l'AAC raw ne se décode pas.
	if (extradata != nullptr && extradata_size > 0)
	{
		ctx->extradata = (uint8_t*)av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
		if (ctx->extradata)
		{
			memcpy(ctx->extradata, extradata, extradata_size);
			ctx->extradata_size = extradata_size;
		}
	}

	if (avcodec_open2(ctx, codec, nullptr) < 0)
	{
		Error("[%s] could not open decoder\n", codec->name);
		avcodec_free_context(&ctx);
		return;
	}

	frame = av_frame_alloc();
	pkt   = av_packet_alloc();
	if (!frame || !pkt)
	{
		Error("[%s] could not allocate frame/packet\n", codec->name);
		return;
	}

	// 0 si le codec restitue des trames de taille variable ; on annonce alors
	// 20 ms, la granularité attendue par le pipeline RTP (cf. côté encodeur).
	numFrameSamples = ctx->frame_size;
	if (numFrameSamples <= 0 && ctx->sample_rate > 0)
		numFrameSamples = ctx->sample_rate / 50;

	opened = true;
	Log("[%s] decoder open: frame size %d, %d Hz\n",
		codec->name, numFrameSamples, ctx->sample_rate);
}

FfAudioDecoder::~FfAudioDecoder()
{
	if (swr)	swr_free(&swr);
	if (frame)	av_frame_free(&frame);
	if (pkt)	av_packet_free(&pkt);
	if (ctx)	avcodec_free_context(&ctx);
}

DWORD FfAudioDecoder::GetRate()
{
	DWORD r = (ctx && ctx->sample_rate) ? (DWORD)ctx->sample_rate : 0;
	return r;
}

DWORD FfAudioDecoder::TrySetRate(DWORD rate)
{
	// Un décodeur restitue à la fréquence native du flux : on ne la force pas.
	// La remise à la fréquence cible est gérée en amont par le mixeur.
	DWORD r = GetRate();
	return r ? r : rate;
}

bool FfAudioDecoder::PublishS16Mono(AVFrame *src)
{
	// Vol du tampon : le contenu passe dans une trame NEUVE, `src` redevient
	// vierge pour le prochain avcodec_receive_frame. Aucune recopie, et jamais
	// de référence sur la trame interne du décodeur (piège déjà écarté en vidéo).
	AVFrame *owned = av_frame_alloc();
	if (!owned)
	{
		Error("[%s] could not allocate output frame\n", codec->name);
		return false;
	}

	av_frame_move_ref(owned, src);

	SamplesPtr samples = Samples::FromAVFrame(owned);
	if (!samples)
	{
		av_frame_free(&owned);
		return false;
	}

	frames.push_back(samples);
	return true;
}

bool FfAudioDecoder::ConvertAndPublish(AVFrame *src)
{
	// Conversion vers S16 mono, à fréquence inchangée : le décodeur restitue
	// toujours la fréquence native du flux, c'est l'aval qui rééchantillonne.
	if (!swr)
	{
		AVChannelLayout mono;
		av_channel_layout_default(&mono, 1);
		int err = swr_alloc_set_opts2(&swr,
			&mono, AV_SAMPLE_FMT_S16, src->sample_rate,
			&src->ch_layout, (AVSampleFormat)src->format, src->sample_rate,
			0, nullptr);
		av_channel_layout_uninit(&mono);

		if (err < 0 || swr_init(swr) < 0)
		{
			Error("[%s] failed to configure decoder resampler\n", codec->name);
			if (swr)
				swr_free(&swr);
			return false;
		}
	}

	// Fréquence inchangée : la capacité de sortie est ce que swr retient
	// encore plus la nouvelle entrée.
	const int cap = (int)swr_get_delay(swr, src->sample_rate) + src->nb_samples;

	SamplesPtr samples = Samples::Alloc((DWORD)cap, (DWORD)src->sample_rate);
	if (!samples)
	{
		Error("[%s] could not allocate %d samples\n", codec->name, cap);
		return false;
	}

	AVFrame *dst = samples->GetAVFrame();
	const int got = swr_convert(swr, dst->data, cap,
		(const uint8_t**)src->extended_data, src->nb_samples);
	if (got <= 0)
		return false;

	// La trame porte le nombre RÉELLEMENT produit, pas la capacité allouée.
	dst->nb_samples = got;
	dst->pts        = src->pts;

	frames.push_back(samples);
	return true;
}

int FfAudioDecoder::Decode(BYTE *in, int inLen)
{
	if (!opened)
		return Error("[%s] decoder not opened\n", codec ? codec->name : "?");

	if (inLen <= 0 || !in)
		return 0;

	// Paquet non compté en références : ffmpeg recopiera si besoin.
	pkt->data = (uint8_t*)in;
	pkt->size = inLen;

	int ret = avcodec_send_packet(ctx, pkt);
	if (ret < 0)
		return Error("[%s] avcodec_send_packet: %d\n", codec->name, ret);

	// Un paquet peut donner 0, 1 ou plusieurs trames : elles sont TOUTES
	// publiées, à leur taille propre. Plus rien n'est tronqué ni redécoupé.
	while ((ret = avcodec_receive_frame(ctx, frame)) >= 0)
	{
		if (Samples::IsS16Mono(frame))
			PublishS16Mono(frame);
		else
			ConvertAndPublish(frame);

		av_frame_unref(frame);

		// Garde-fou : un consommateur qui n'appelle jamais GetFrame() ne doit
		// pas faire enfler la file. Au-delà d'une seconde, on jette le plus
		// ancien — la même politique que la fifo de l'encodeur.
		while (frames.size() > MaxPendingFrames)
		{
			Error("[%s] frame queue overflow, dropping oldest\n", codec->name);
			frames.pop_front();
		}
	}

	if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
		return Error("[%s] avcodec_receive_frame: %d\n", codec->name, ret);

	return (int)frames.size();
}

SamplesPtr FfAudioDecoder::GetFrame()
{
	if (frames.empty())
		return nullptr;

	SamplesPtr samples = frames.front();
	frames.pop_front();
	return samples;
}
