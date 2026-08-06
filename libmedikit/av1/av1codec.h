/*
 * File:   av1codec.h
 *
 * Encodeur/décodeur AV1 sur ffmpeg. Backend forcé "libsvtav1" côté encodeur
 * (temps-réel capable ; avcodec_find_encoder(AV_CODEC_ID_AV1) renvoie par
 * défaut "libaom-av1", bien plus lent) et "libdav1d" côté décodeur (déjà le
 * choix par défaut de ffmpeg pour AV1, forcé ici par explicité plutôt que par
 * dépendance à l'ordre de résolution interne de ffmpeg).
 *
 * NOTE : la packetisation RTP (agrégation d'OBU, RFC "AV1 RTP Payload
 * Format") n'est pas encore implémentée ici — PacketizeFrame() ne fait
 * aujourd'hui que retirer l'OBU de temporal delimiter (qui ne doit jamais
 * être transmis) et capturer le sequence header OBU pour GetFmtpInfo, avant
 * de retomber sur la packetisation par défaut de FfVideoEncoder (non
 * conforme au format RTP AV1). AV1Decoder n'a pas non plus de dépaquetiseur
 * dédié pour l'instant (hérite de l'accumulation brute par défaut). À
 * corriger dans un second temps par un dépaquetiseur/paquetiseur OBU dédié.
 */
#ifndef _AV1CODEC_H_
#define _AV1CODEC_H_

#include "../ffvideocodec.h"
#include <string>
#include <vector>
#include <cstdint>

class AV1Encoder : public FfVideoEncoder
{
public:
	AV1Encoder(const Properties& properties);
	virtual ~AV1Encoder();
	// profile/level-idx/tier (spec AV1 RTP payload format), capturés depuis le
	// premier sequence header OBU vu dans le flux encodé.
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);

	// Paramètres fmtp SDP (SANS "a=fmtp:<pt> ") dérivés de la seule config
	// (av1.profile / av1.level-idx / av1.tier + défauts), sans codec ouvert.
	// Forme de négociation ; la forme instance ci-dessus reste dérivée du
	// sequence header réel. cf. nego_fmtp décision E.
	static std::string GetFmtpParams(const Properties& properties);

	// Ingestion du fmtp distant (phase 5b nego_fmtp, spec AV1 RTP payload §7.2) :
	// l'asymétrie est le DÉFAUT — on annonce toujours notre propre capacité,
	// rien à refléter (`announceProps` = localProps inchangées). Le sens
	// émission est normatif : « MUST be encoded with a profile, level and tier
	// lesser or equal to the values declared by the receiving agent » —
	// `effectiveProps` = minimum composante par composante. Les paramètres
	// omis d'un fmtp PRÉSENT valent leurs défauts (0 / 5 / 0) ; une map
	// distante VIDE (rien relayé par le contrôleur) n'apporte aucune
	// contrainte — un pair AV1 sans ligne fmtp déclare formellement 0/5/0,
	// mais c'est indistinguable d'un contrôleur qui ne relaie pas, et le
	// contrôleur qui veut la lecture stricte relaie les défauts explicitement.
	static void ResolveNegotiation(const Properties& localProps,
	                               const std::map<std::string,std::string>& remoteParams,
	                               Properties& announceProps,
	                               Properties& effectiveProps);

	// Écrêtage cadence/taille au niveau AV1 borné (annexe A.3 de la spec
	// bitstream, décidé le 2026-08-06) : lit `av1.level-idx` dans `properties`
	// (les effectiveProps fusionnées) et ramène (width, height, fps) dans
	// MaxPicSize / MaxHSize / MaxVSize / MaxDisplayRate — taille d'abord
	// (ratio conservé, arrondi pair), cadence ensuite. Sans clé, ne touche à
	// rien. Rend true si quelque chose a été écrêté (et le journalise).
	static bool ClampToLevel(const Properties& properties,
	                         int& width, int& height, int& fps);

protected:
	virtual void ConfigureContext();
	virtual void PacketizeFrame();

private:
	int  preset;	// av1.preset : SVT-AV1 -1..13 (plus petit = plus lent/meilleur)

	bool obuSeqHdrCached;
	int  cachedProfile;
	int  cachedLevelIdx;
	int  cachedTier;
};

class AV1Decoder : public FfVideoDecoder
{
public:
	AV1Decoder();
	virtual ~AV1Decoder();

	static bool IsSupported() { return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_AV1, "libdav1d"); }
};

#endif
