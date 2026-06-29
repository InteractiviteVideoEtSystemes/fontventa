#include "ffaudiocodec.h"
extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#include "medkit/log.h"
#include <string.h>

FfAudioEncoder::FfAudioEncoder(const Properties& properties, enum AVCodecID av_codec, AudioCodec::Type codec_id) :
	defaultSampleRate(8000), codec(nullptr), ctx(nullptr), swr(nullptr),
	frame(nullptr), pkt(nullptr), opened(false)
{
	// Membres hérités d'AudioEncoder
	type = codec_id;
	numFrameSamples = 0;

	// Recherche de l'encodeur
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

	const bool s16ok  = IsSigned16FmtSupported();
	const bool rateok = IsRateNativelySupported(rate);

	// L'entrée du mediaserver est toujours du S16 mono. On encode en S16 si
	// le codec l'accepte, sinon dans son premier format natif via un resampler.
	ctx->sample_fmt  = s16ok  ? AV_SAMPLE_FMT_S16 : codec->sample_fmts[0];
	ctx->sample_rate = rateok ? (int)rate : (int)defaultSampleRate;

	// On repart d'un resampler propre à chaque (re)configuration.
	if (swr)
		swr_free(&swr);

	if (!s16ok || !rateok)
	{
		Log("[%s]: S16/%u Hz non supporté nativement -> resampler vers %s/%d Hz\n",
			codec->name, rate, av_get_sample_fmt_name(ctx->sample_fmt), ctx->sample_rate);

		AVChannelLayout mono;
		av_channel_layout_default(&mono, 1);

		int err = swr_alloc_set_opts2(&swr,
			&mono, ctx->sample_fmt,  ctx->sample_rate,	// sortie
			&mono, AV_SAMPLE_FMT_S16, (int)rate,		// entrée
			0, nullptr);

		if (err < 0 || swr_init(swr) < 0)
		{
			Error("[%s] failed to configure resampler %u Hz -> %d Hz\n",
				codec->name, rate, ctx->sample_rate);
			if (swr)
				swr_free(&swr);
			av_channel_layout_uninit(&mono);
			return defaultSampleRate;
		}
		av_channel_layout_uninit(&mono);
	}

	return ctx->sample_rate;
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

	// frame_size n'est connu qu'après ouverture (0 = trame de taille variable).
	numFrameSamples = ctx->frame_size;

	frame = av_frame_alloc();
	pkt   = av_packet_alloc();
	if (!frame || !pkt)
	{
		Error("[%s] could not allocate frame/packet\n", codec->name);
		return false;
	}

	frame->format      = ctx->sample_fmt;
	frame->sample_rate = ctx->sample_rate;
	av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);

	if (numFrameSamples > 0)
	{
		frame->nb_samples = numFrameSamples;
		if (av_frame_get_buffer(frame, 0) < 0)
		{
			Error("[%s] could not allocate frame buffer\n", codec->name);
			return false;
		}
	}

	opened = true;
	Log("[%s] encoder open: frame size %d, %d Hz, fmt %s\n",
		codec->name, numFrameSamples, ctx->sample_rate, av_get_sample_fmt_name(ctx->sample_fmt));
	return true;
}

int FfAudioEncoder::Encode(SWORD *in, int inLen, BYTE* out, int outLen)
{
	if (!opened && !Open())
		return Error("[%s] encoder not opened\n", codec ? codec->name : "?");

	if (inLen <= 0)
		return 0;

	if (av_frame_make_writable(frame) < 0)
		return Error("[%s] frame not writable\n", codec->name);

	// Remplissage de la trame d'entrée.
	if (swr)
	{
		// Rééchantillonnage S16/rate-entrée -> sample_fmt/sample_rate du codec.
		const uint8_t* src[1] = { (const uint8_t*)in };
		int produced = swr_convert(swr, frame->data, frame->nb_samples, src, inLen);
		if (produced < 0)
			return Error("[%s] resampling failed\n", codec->name);
		frame->nb_samples = produced;
	}
	else
	{
		if (numFrameSamples > 0 && inLen != numFrameSamples)
			return Error("[%s] sample count %d != frame size %d\n",
				codec->name, inLen, numFrameSamples);
		// S16 mono direct.
		memcpy(frame->data[0], in, (size_t)inLen * sizeof(SWORD));
		frame->nb_samples = inLen;
	}

	int ret = avcodec_send_frame(ctx, frame);
	if (ret < 0)
		return Error("[%s] avcodec_send_frame: %d\n", codec->name, ret);

	int total = 0;
	while ((ret = avcodec_receive_packet(ctx, pkt)) >= 0)
	{
		if (total + pkt->size <= outLen)
		{
			memcpy(out + total, pkt->data, pkt->size);
			total += pkt->size;
		}
		else
		{
			Error("[%s] output buffer too small (need %d, have %d)\n",
				codec->name, total + pkt->size, outLen);
		}
		av_packet_unref(pkt);
	}

	if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
		return Error("[%s] avcodec_receive_packet: %d\n", codec->name, ret);

	return total;
}

/******************************************************************************
 *                              FfAudioDecoder                                *
 ******************************************************************************/

FfAudioDecoder::FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id) :
	codec(nullptr), ctx(nullptr), swr(nullptr), frame(nullptr), pkt(nullptr), opened(false)
{
	// Membres hérités d'AudioDecoder
	type = codec_id;
	numFrameSamples = 0;

	// Recherche du décodeur
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

	// 0 si le codec restitue des trames de taille variable.
	numFrameSamples = ctx->frame_size;

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

int FfAudioDecoder::Decode(BYTE *in, int inLen, SWORD* out, int outLen)
{
	if (!opened)
		return Error("[%s] decoder not opened\n", codec ? codec->name : "?");

	if (inLen > 0)
	{
		// Paquet non compté en références : ffmpeg recopiera si besoin.
		pkt->data = (uint8_t*)in;
		pkt->size = inLen;

		int ret = avcodec_send_packet(ctx, pkt);
		if (ret < 0)
			return Error("[%s] avcodec_send_packet: %d\n", codec->name, ret);

		// Un paquet peut donner 0, 1 ou plusieurs trames.
		while ((ret = avcodec_receive_frame(ctx, frame)) >= 0)
		{
			const bool mono = (frame->ch_layout.nb_channels == 1);

			if (mono && frame->format == AV_SAMPLE_FMT_S16)
			{
				// Déjà au bon format : empilage direct.
				samples.push((SWORD*)frame->extended_data[0], frame->nb_samples);
			}
			else
			{
				// Conversion vers S16 mono (même fréquence : pas de rééchantillonnage).
				if (!swr)
				{
					AVChannelLayout mono_out;
					av_channel_layout_default(&mono_out, 1);
					int err = swr_alloc_set_opts2(&swr,
						&mono_out, AV_SAMPLE_FMT_S16,        frame->sample_rate,
						&frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate,
						0, nullptr);
					av_channel_layout_uninit(&mono_out);

					if (err < 0 || swr_init(swr) < 0)
					{
						Error("[%s] failed to configure decoder resampler\n", codec->name);
						if (swr)
							swr_free(&swr);
						av_frame_unref(frame);
						continue;
					}
				}

				SWORD conv[8192];
				uint8_t* outp[1] = { (uint8_t*)conv };
				const int cap = (int)(sizeof(conv) / sizeof(SWORD));
				int n = swr_convert(swr, outp, cap,
					(const uint8_t**)frame->extended_data, frame->nb_samples);
				if (n > 0)
					samples.push(conv, n);
			}

			av_frame_unref(frame);
		}

		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
			return Error("[%s] avcodec_receive_frame: %d\n", codec->name, ret);
	}

	// Restitution par tranches de numFrameSamples (ou ce qui est disponible
	// pour les codecs à trame variable), sans déborder le tampon appelant.
	int want = numFrameSamples;
	if (want <= 0)
		want = samples.length();
	if (want > outLen)
		want = outLen;
	if (want <= 0 || samples.length() < want)
		return 0;

	samples.pop(out, want);
	return want;
}
