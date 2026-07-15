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
};

#endif
