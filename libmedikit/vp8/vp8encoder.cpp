/*
 * File:   vp8encoder.cpp
 *
 * Encodeur VP8 via le wrapper libvpx de ffmpeg (AV_CODEC_ID_VP8).
 */
#include "vp8encoder.h"

extern "C" {
#include <libavutil/opt.h>
}

// Partagées avec vp8decoder.cpp : construction du fmtp VP8 (max-fr/max-fs,
// RFC 7742) à partir de limites déjà résolues par l'appelant.
extern bool BuildVP8FmtpFromLimits(int payloadType, int maxFrameRate, int maxFrameSize, std::string &fmtp2);
extern std::string BuildVP8FmtpParams(int maxFrameRate, int maxFrameSize);

VP8Encoder::VP8Encoder(const Properties& properties) :
	FfVideoEncoder(properties, AV_CODEC_ID_VP8, VideoCodec::VP8)
{
	maxFrameRate = properties.GetProperty("vp8.max-fr", 0);
	maxFrameSize = properties.GetProperty("vp8.max-fs", 0);
}

void VP8Encoder::ConfigureContext()
{
	if (maxFrameRate > 0)
	{
		ctx->framerate.num = maxFrameRate;
		ctx->framerate.den = 1;
	}

	// Temps réel. Les défauts du wrapper libvpx de ffmpeg sont ceux d'un
	// transcodage de fichier : deadline « good », cpu-used 1, un seul thread,
	// 25 images d'avance pour choisir une trame de référence alternative.
	// Mesuré le 2026-08-29 sur deux cœurs : 125 ms par image 720p, pire cas
	// 782 ms — deux fois et demie le budget d'une source à 20 im/s. Le
	// transcodeur inline encodant sur le thread de démux, ce retard faisait
	// jeter les paquets de la source et réclamer une trame clé par seconde.
	av_opt_set_int(ctx->priv_data, "deadline", 1, 0);	// VPX_DL_REALTIME
	av_opt_set_int(ctx->priv_data, "cpu-used", 6, 0);
	av_opt_set_int(ctx->priv_data, "lag-in-frames", 0, 0);
	// Trames codées pour tolérer la perte : sur RTP, une trame manquante ne
	// doit pas rendre les suivantes indécodables jusqu'à la prochaine clé.
	av_opt_set(ctx->priv_data, "error-resilient", "default", 0);
	// Deux threads au plus : le reste de la machine sert au décodage, à l'autre
	// sens et aux jambes RTP.
	ctx->thread_count = 2;
}

int VP8Encoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Mémorise fps/débit/période intra
	FfVideoEncoder::SetFrameRate(frames,kbits,intraPeriod);

	// Politiques communes (FfVideoEncoder) : le débit baisse tout de suite et
	// monte par paliers ; la cadence n'est lue qu'à l'ouverture, donc seule une
	// réouverture la change. Une SEULE réouverture porte les deux — chaque
	// réouverture coûte une trame clé.
	if (ShouldReopenForBitrate() || ShouldReopenForFps())
		ReopenCodec();

	return 1;
}

VP8Encoder::~VP8Encoder()
{
}

/***********************
* VP8Encoder::PacketizeFrame
*	Packetisation RTP VP8 (RFC 7741). Chaque fragment est préfixé d'un VP8 payload
*	descriptor minimal :
*	   0 1 2 3 4 5 6 7
*	  +-+-+-+-+-+-+-+-+
*	  |X|R|N|S|PartID |   X=0,R=0,N=0 ; S=1 sur le 1er paquet de la trame (début
*	  +-+-+-+-+-+-+-+-+   de partition), 0 ensuite ; PartID=0.
************************/
void VP8Encoder::PacketizeFrame()
{
	DWORD len = frame->GetLength();
	DWORD ini = 0;
	BYTE desc = 0x10;	// S=1 (start of partition) sur le premier fragment

	while (ini < len)
	{
		bool mark = false;
		DWORD lenpkt = RTPPAYLOADSIZE-1;	// 1 octet de descripteur
		if (lenpkt+ini >= len)
		{
			mark = true;	// dernier fragment de la trame -> marker RTP
			lenpkt = len-ini;
		}

		frame->AddRtpPacket(ini, lenpkt, &desc, 1, mark);

		// Fragments suivants : S=0.
		desc = 0x00;

		ini += lenpkt;
	}
}

bool VP8Encoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	return BuildVP8FmtpFromLimits(payloadType, maxFrameRate, maxFrameSize, fmtp);
}

std::string VP8Encoder::GetFmtpParams(const Properties& properties)
{
	// Mêmes clés/défauts que le constructeur, sans instancier d'encodeur.
	return BuildVP8FmtpParams(
		properties.GetProperty("vp8.max-fr", 0),
		properties.GetProperty("vp8.max-fs", 0));
}
