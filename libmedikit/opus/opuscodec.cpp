/*
 * OPUS via FfAudioEncoder/FfAudioDecoder (ffmpeg), backend "libopus".
 */
#include "opuscodec.h"
#include <medkit/log.h>
#include <sstream>

extern "C" {
#include <libavutil/opt.h>
}

/******************************** OPUSEncoder ********************************/

OPUSEncoder::OPUSEncoder(const Properties &properties) :
	// "libopus" : seul backend exposant fec/packet_loss/vbr nécessaires à la
	// négociation SDP (cf. commentaire opuscodec.h).
	FfAudioEncoder(properties, AV_CODEC_ID_OPUS, AudioCodec::OPUS, "libopus")
{
	useInbandFec      = (bool) properties.GetProperty("opus.useinbandfec", 0);
	useDtx            = (bool) properties.GetProperty("opus.usedtx", 0);
	maxAverageBitrate = properties.GetProperty("opus.maxaveragebitrate", 0);
	cbr               = (bool) properties.GetProperty("opus.cbr", 0);
	packetLossPerc    = properties.GetProperty("opus.packet-loss-perc", 10);

	// Opus opère toujours à 48000 Hz dans le pipeline MCU (horloge RTP fixe).
	defaultSampleRate = 48000;
	TrySetRate(48000);

	// Options privées libopus : à poser sur ctx->priv_data avant Open()
	// (avcodec_open2). Le contexte est déjà alloué par FfAudioEncoder.
	if (ctx)
	{
		if (useInbandFec)
		{
			// "fec" est un no-op côté ffmpeg tant que "packet_loss" est nul.
			av_opt_set_int(ctx->priv_data, "packet_loss", packetLossPerc, 0);
			av_opt_set_int(ctx->priv_data, "fec", 1, 0);
		}
		if (cbr)
			// vbr=0 -> CBR ; VBR (1) est la valeur par défaut de ffmpeg.
			av_opt_set_int(ctx->priv_data, "vbr", 0, 0);
		if (maxAverageBitrate > 0)
			ctx->bit_rate = maxAverageBitrate;
	}

	// frame_size pour 20 ms à 48000 Hz = 960 ; repli défensif si variable.
	Open();
	if (numFrameSamples <= 0)
		numFrameSamples = 960;
}

DWORD OPUSEncoder::TrySetRate(DWORD /*rate*/)
{
	// L'horloge RTP d'OPUS est fixée à 48000 Hz (RFC 7587).
	// On force toujours 48000 Hz, quelle que soit la fréquence demandée par
	// le MCU. GetRate() retourne alors 48000 et le MCU adapte sa cadence.
	return FfAudioEncoder::TrySetRate(48000);
}

bool OPUSEncoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	std::ostringstream f;
	f << "a=fmtp:" << payloadType
	  << " useinbandfec=" << (useInbandFec ? 1 : 0)
	  << ";usedtx=" << (useDtx ? 1 : 0);
	if (maxAverageBitrate > 0)
		f << ";maxaveragebitrate=" << maxAverageBitrate;
	if (cbr)
		f << ";cbr=1";

	fmtp = f.str();
	return true;
}

/******************************** OPUSDecoder ********************************/

OPUSDecoder::OPUSDecoder() :
	FfAudioDecoder(AV_CODEC_ID_OPUS, AudioCodec::OPUS, "libopus")
{
	// Le décodeur libopus produit du PCM 48000 Hz.
	if (numFrameSamples <= 0)
		numFrameSamples = 960;
}
