#ifndef _H264ENCODER_H_
#define _H264ENCODER_H_
#include "../medkit/codecs.h"
#include "../medkit/video.h"
#include "../ffvideocodec.h"
#include <string>
#include <vector>
#include <map>
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

	// Résolution offer/answer H.264 (RFC 6184 §8.2.2), sans codec ouvert.
	//
	// `remoteParams` = les paramètres fmtp du pair déjà découpés (vide s'il n'en a
	// pas envoyé, cas d'un offer sortant ou d'un pair muet). Produit DEUX jeux de
	// propriétés, qu'il ne faut surtout pas confondre :
	//
	//   announceProps  = ce que NOUS annonçons dans le SDP, c'est-à-dire notre
	//                    capacité de RÉCEPTION (ce que nous savons décoder) ;
	//   effectiveProps = ce qui BORNE NOTRE ENCODEUR, c'est-à-dire ce que le PAIR
	//                    a déclaré savoir décoder.
	//
	// Règles (cf. mcu_module.md §16.3.4 (b), nego_fmtp.md phase 5) : le profil
	// annoncé est celui de l'offre ; le niveau annoncé n'est le nôtre que si
	// `level-asymmetry-allowed=1` est présent des deux côtés, sinon c'est celui de
	// l'offre — sauf si nous ne savons pas le décoder, cas où nous annonçons NOTRE
	// maximum et le signalons (écart assumé à la RFC, qui ne laisse que refléter ou
	// retirer : refuser la vidéo est un échec plus dur, et annoncer en dessous est
	// justement ce dont un pair correct a besoin pour encoder à notre portée).
	static void ResolveNegotiation(const Properties& localProps,
	                               const std::map<std::string,std::string>& remoteParams,
	                               Properties& announceProps,
	                               Properties& effectiveProps);

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
