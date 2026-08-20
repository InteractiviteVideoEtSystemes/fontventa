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

	// Paramètres fmtp SDP (SANS "a=fmtp:<pt> ") dérivés de la seule config
	// (vp8.max-fr / vp8.max-fs), sans codec ouvert. cf. nego_fmtp décision E.
	static std::string GetFmtpParams(const Properties& properties);

	// Reconfiguration à chaud : le wrapper libvpx de ffmpeg ne relit JAMAIS
	// ctx->bit_rate en cours de route (mesuré : débit inchangé après une
	// consigne divisée par 10 ; libx264 le fait, mais seulement si le VBV était
	// armé à l'ouverture), donc la mémorisation de FfVideoEncoder::SetFrameRate
	// ne suffit pas — sans ceci l'encodeur garde son débit d'ouverture pour
	// toute sa vie. La réouverture est le seul levier ; elle coûte une trame
	// clé, d'où la politique asymétrique de ShouldReopenForBitrate.
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod);

protected:
	// Packetisation RTP VP8 (RFC 7741) : préfixe chaque fragment d'un VP8 payload
	// descriptor minimal (S=1 sur le 1er paquet de la trame, 0 ensuite).
	virtual void PacketizeFrame();
	virtual void ConfigureContext();

	int maxFrameSize;
	int maxFrameRate;
};

#endif
