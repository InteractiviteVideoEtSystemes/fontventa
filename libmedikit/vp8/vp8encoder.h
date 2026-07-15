/*
 * File:   vp8encoder.h
 *
 * Encodeur VP8 adossé à FfVideoEncoder : ffmpeg n'a pas d'encodeur VP8 natif,
 * mais expose le wrapper `libvpx` (avcodec_find_encoder(AV_CODEC_ID_VP8) le
 * renvoie par défaut). On encode donc le VP8 via ffmpeg/libvpx, sans dépendance
 * -lvpx directe côté mediaserver.
 *
 * NOTE : la packetisation RTP héritée de FfVideoEncoder::EncodeFrame est de type
 * H263 (préfixe 0x04/0x00) — non conforme au VP8 payload descriptor (RFC 7741).
 * À corriger dans un second temps (packetiseur VP8 dédié), symétrique au
 * dépaquetiseur VP8Decoder::DecodePacket.
 */
#ifndef _VP8ENCODER_H_
#define _VP8ENCODER_H_

#include "../ffvideocodec.h"
#include <string>

class VP8Encoder : public FfVideoEncoder
{
public:
	VP8Encoder(const Properties& properties);
	virtual ~VP8Encoder();
	// max-fr/max-fs (RFC 7742) reflètent directement maxFrameRate/maxFrameSize
	// ci-dessous : les vraies limites configurées pour cet encodeur.
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);

protected:
	// Packetisation RTP VP8 (RFC 7741) : préfixe chaque fragment d'un VP8 payload
	// descriptor minimal (S=1 sur le 1er paquet de la trame, 0 ensuite).
	virtual void PacketizeFrame();
	virtual void ConfigureContext();

	int maxFrameSize;
	int maxFrameRate;
};

#endif
