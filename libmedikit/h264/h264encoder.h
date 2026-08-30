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
	virtual VideoFramePtr EncodeFrame(PictPtr pic);
	// Reconfiguration à chaud (adaptation dynamique de débit) : mise à jour
	// du VBV relue par libx264 à chaque trame, ou réouverture throttlée en
	// VAAPI (qui ne sait pas changer son rate control en cours de route).
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod);
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);

	// h264_vaapi n'est qu'un bonus : c'est la disponibilite de l'encodeur
	// LOGICIEL (libx264) qui decide du support.
	static bool IsSupported() { return FfVideoEncoder::IsCodecAvailable(AV_CODEC_ID_H264); }

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
	//
	// `packetization-mode` suit une règle plus simple : celui du pair, dans LES DEUX
	// jeux. Il fait partie de l'identité du payload type côté pair (un PT offert en
	// mode 0 et répondu en mode 1 n'est pas le codec qu'il a proposé — refus sec côté
	// navigateur), et émettre dans un mode qu'il ne dépaquettise pas ne produit rien
	// de décodable. À défaut d'entrée distante, notre mode 1.
	//
	// La résolution est **par payload type** : l'appelant passe les params du PT en
	// cours, jamais ceux d'un autre PT du même codec.
	static void ResolveNegotiation(const Properties& localProps,
	                               const std::map<std::string,std::string>& remoteParams,
	                               Properties& announceProps,
	                               Properties& effectiveProps);

	// Le mode de paquetisation à honorer à l'émission, lu de `h264.packetization-mode`.
	//
	// **1 par défaut, y compris quand le pair n'a rien déclaré** : écart assumé à la RFC
	// 6184 §8.1 (qui fait valoir 0 en l'absence du paramètre), décidé le 2026-08-06. Un
	// pair qui omet le paramètre est un SDP incomplet plus qu'un décodeur single-NAL —
	// tout décodeur moderne dépaquettise le FU-A. Si le pari est faux le symptôme est
	// franc (H.264 négocié, pas d'image chez ce client) et la branche mode 0 existe.
	static int WantedPacketizationMode(const Properties& properties);

	// Le mode 0 interdit le FU-A : chaque NALU doit tenir dans un paquet RTP, ce que
	// seul libx264 sait garantir (`slice-max-size`, absent des encodeurs VAAPI). Retourne
	// donc false — et le journalise — quand le mode 0 est demandé.
	static bool WantsHardware(const Properties& properties);

	// CRF (libx264) selon le budget par pixel et par image
	// (bpp = débit / (largeur × hauteur × fps)) : 21 au-dessus de 0,08 bpp
	// (~2,2 Mb/s en 720p30) où le VBV borne de toute façon, 26 sous 0,04 bpp
	// pour éviter un VBV en butée permanente (pompage de qualité), 23 entre
	// les deux. `current` = CRF en vigueur : le seuil de SORTIE du régime
	// courant est décalé de ~10 % (hystérésis contre le battement de la
	// boucle d'adaptation autour de son plateau). bpp inconnu (<= 0) rend
	// `current` tel quel.
	static int CrfForBudget(double bpp, int current);

protected:
	virtual void ConfigureContext();
	virtual void PacketizeFrame(VideoFrame& frame);

private:
	void GetProfileLevel(int &profile, int &level);
	// Variante instance : bpp calculé de l'état courant (bitrate, taille, fps).
	int CrfForBudget(int current) const;

	std::string h264ProfileLevelId;
	bool intraRefresh;
	int qPel;
	// CRF appliqué à l'encodeur libx264 ouvert : l'option privée `crf` du
	// wrapper est relue à chaque trame (x264_encoder_reconfig), donc suivie à
	// chaud sans réouverture ni IDR — cf. SetFrameRate.
	int crfApplied;
	// Mode de paquetisation négocié pour l'ÉMISSION (0 ou 1) : borne la taille des
	// slices produites, cf. ConfigureContext.
	int packetizationMode;
	// Débit effectif à l'ouverture : sert à décider une réouverture VAAPI

	// SPS/PPS (bruts, sans start code) du premier NAL vu par PacketizeFrame,
	// capturés APRÈS réécriture du profile-level-id négocié (cf.
	// PacketizeFrame) : source de GetFmtpInfo, ctx->extradata n'étant jamais
	// peuplé ici. Invalidés à chaque (ré)ouverture par ConfigureContext().
	std::vector<uint8_t> cachedSps;
	std::vector<uint8_t> cachedPps;
	bool spsPpsCached;
};

#endif
