#ifndef _H264ENCODER_H_
#define _H264ENCODER_H_
#include "../medkit/codecs.h"
#include "../medkit/video.h"
#include "../ffvideocodec.h"
#include <string>
#include <vector>
#include <cstdint>

// Encodeur H264 sur ffmpeg : h264_vaapi si un device VAAPI est utilisable,
// libx264 en repli. Remplace l'ancien encodeur x264 direct.
class H264Encoder : public FfVideoEncoder
{
public:
	H264Encoder(const Properties& properties);
	virtual ~H264Encoder();
	virtual VideoFrame* EncodeFrame(PictPtr pic);
	// Reconfiguration à chaud (adaptation dynamique de débit) : mise à jour
	// du VBV relue par libx264 à chaque trame, ou réouverture throttlée en
	// VAAPI (qui ne sait pas changer son rate control en cours de route).
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod);
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);

	// Paramètres fmtp SDP (SANS "a=fmtp:<pt> ") dérivés de la seule config, sans
	// codec ouvert : profile-level-id (h264.profile-level-id) + packetization-mode.
	// sprop-parameter-sets est OMIS car les SPS/PPS ne sont connus qu'après
	// l'encodage d'une trame (indispo à la négociation) ; le pair les reçoit
	// in-band, l'offer best-effort s'en passe (cf. nego_fmtp §3.2). cf. décision E.
	static std::string GetFmtpParams(const Properties& properties);

protected:
	virtual void ConfigureContext();
	virtual void PacketizeFrame();

private:
	void GetProfileLevel(int &profile, int &level);

	std::string h264ProfileLevelId;
	bool intraRefresh;
	int qPel;
	// Débit effectif à l'ouverture : sert à décider une réouverture VAAPI
	int openedBitrate;

	// SPS/PPS (bruts, sans start code) du premier NAL vu par PacketizeFrame,
	// capturés APRÈS réécriture du profile-level-id négocié (cf.
	// PacketizeFrame) : source de GetFmtpInfo, ctx->extradata n'étant jamais
	// peuplé ici. Invalidés à chaque (ré)ouverture par ConfigureContext().
	std::vector<uint8_t> cachedSps;
	std::vector<uint8_t> cachedPps;
	bool spsPpsCached;
};

#endif
