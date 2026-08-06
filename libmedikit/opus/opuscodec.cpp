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

// Formate les paramètres fmtp OPUS (sans "a=fmtp:<pt> "). Source unique partagée
// par la forme « ligne complète » (GetFmtpInfo) et la forme « params seuls »
// (GetFmtpParams), pour éviter toute dérive entre les deux.
static std::string OpusFmtpParams(bool useInbandFec, bool useDtx, int maxAverageBitrate, bool cbr)
{
	std::ostringstream f;
	f << "useinbandfec=" << (useInbandFec ? 1 : 0)
	  << ";usedtx=" << (useDtx ? 1 : 0);
	if (maxAverageBitrate > 0)
		f << ";maxaveragebitrate=" << maxAverageBitrate;
	if (cbr)
		f << ";cbr=1";
	return f.str();
}

bool OPUSEncoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	std::ostringstream f;
	f << "a=fmtp:" << payloadType << " "
	  << OpusFmtpParams(useInbandFec, useDtx, maxAverageBitrate, cbr);
	fmtp = f.str();
	return true;
}

std::string OPUSEncoder::GetFmtpParams(const Properties &properties)
{
	// Mêmes clés/défauts que le constructeur, sans instancier d'encodeur.
	return OpusFmtpParams(
		(bool) properties.GetProperty("opus.useinbandfec", 0),
		(bool) properties.GetProperty("opus.usedtx", 0),
		properties.GetProperty("opus.maxaveragebitrate", 0),
		(bool) properties.GetProperty("opus.cbr", 0));
}

void OPUSEncoder::ResolveNegotiation(const Properties& localProps,
                                     const std::map<std::string,std::string>& remoteParams,
                                     Properties& announceProps,
                                     Properties& effectiveProps)
{
	// Tout ce que la négociation ne touche pas doit survivre des deux côtés,
	// et l'annonce N'EST PAS un reflet : RFC 7587 §7 fait de chaque paramètre
	// la préférence de réception de celui qui l'écrit. On annonce donc les
	// nôtres telles quelles ; seul le sens émission est résolu ici.
	announceProps  = localProps;
	effectiveProps = localProps;

	// Un paramètre que le pair n'a PAS écrit n'exprime aucune préférence
	// exploitable côté flags (useinbandfec absent = « n'en a pas besoin »,
	// mais en envoyer reste légal) : on n'écrase la config locale que sur
	// déclaration explicite. Écrire via operator[] et non SetProperty(), qui
	// fait un insert et n'écraserait pas la valeur héritée.
	std::map<std::string,std::string>::const_iterator it;

	// FEC en bande : l'émettre vers un pair qui ne l'a pas demandée gaspille
	// du débit ; vers un pair qui la demande, c'est sa résilience aux pertes.
	if ((it = remoteParams.find("useinbandfec")) != remoteParams.end())
		effectiveProps["opus.useinbandfec"] = it->second;

	if ((it = remoteParams.find("usedtx")) != remoteParams.end())
		effectiveProps["opus.usedtx"] = it->second;

	if ((it = remoteParams.find("cbr")) != remoteParams.end())
		effectiveProps["opus.cbr"] = it->second;

	// maxaveragebitrate (b/s) est une BORNE dure du récepteur : émettre
	// au-dessus produit un flux négocié et écrêté nulle part. min() avec une
	// éventuelle borne locale ; 0 local = pas de borne à nous.
	if ((it = remoteParams.find("maxaveragebitrate")) != remoteParams.end())
	{
		const int peer  = atoi(it->second.c_str());
		const int local = localProps.GetProperty("opus.maxaveragebitrate", 0);

		if (peer > 0)
		{
			char value[16];
			snprintf(value, sizeof(value), "%d",
			         (local > 0 && local < peer) ? local : peer);
			effectiveProps["opus.maxaveragebitrate"] = value;
		}
	}
}

/******************************** OPUSDecoder ********************************/

OPUSDecoder::OPUSDecoder() :
	FfAudioDecoder(AV_CODEC_ID_OPUS, AudioCodec::OPUS, "libopus")
{
	// Le décodeur libopus produit du PCM 48000 Hz.
	if (numFrameSamples <= 0)
		numFrameSamples = 960;
}
