/*
 * File:   vp8encoder.cpp
 *
 * Encodeur VP8 via le wrapper libvpx de ffmpeg (AV_CODEC_ID_VP8).
 */
#include "vp8encoder.h"

// Partagée avec vp8decoder.cpp : construction générique "a=fmtp:<pt>
// max-fr=X;max-fs=Y" à partir de limites déjà résolues par l'appelant.
extern bool BuildVP8FmtpFromLimits(int payloadType, int maxFrameRate, int maxFrameSize, std::string &fmtp2);

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
