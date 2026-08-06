/*
 * OPUS via FfAudioEncoder/FfAudioDecoder (ffmpeg).
 *
 * L'horloge RTP d'OPUS est TOUJOURS 48000 Hz (RFC 7587), quelle que soit
 * la fréquence de travail du pipeline MCU. L'encodeur force ctx->sample_rate
 * à 48000 Hz ; le MCU voit GetRate()=48000 et envoie 960 échantillons/trame
 * (20 ms).
 *
 * Backend ffmpeg "libopus" (et non le codec natif "opus") : seul le wrapper
 * libopus expose les options nécessaires à la négociation SDP (fec,
 * packet_loss, vbr/cbr) ; le natif n'a quasiment aucune option privée
 * (opus_delay, apply_phase_inv). Pas de nouvelle dépendance de lien : ffmpeg
 * est buildé --enable-libopus, libopus.so est déjà tiré transitivement par
 * libavcodec.so.
 */
#ifndef OPUSCODEC_H
#define OPUSCODEC_H

#include "../ffaudiocodec.h"
#include <string>

/**
 * Encodeur OPUS : toujours à 48000 Hz. TrySetRate() ignore le taux demandé
 * et retourne 48000, afin que GetRate()/GetClockRate() soient cohérents avec
 * l'horloge RTP fixe d'OPUS.
 *
 * Propriétés négociables (SDP a=fmtp), lues dans le constructeur :
 *   opus.useinbandfec     (bool, def 0) -> AVOption "fec" + "packet_loss"
 *   opus.packet-loss-perc (int,  def 10) -> AVOption "packet_loss" (ffmpeg
 *                            n'active "fec" que si packet_loss != 0)
 *   opus.cbr              (bool, def 0) -> AVOption "vbr"=0 (CBR) ; VBR ffmpeg
 *                            par défaut si absent/0
 *   opus.maxaveragebitrate (int bps, def 0=inchangé) -> ctx->bit_rate
 *   opus.usedtx            (bool, def 0) -> rapporté dans le fmtp uniquement :
 *                            aucune option "dtx" n'est exposée par le wrapper
 *                            libopus de ffmpeg, ne pilote donc rien ici.
 */
class OPUSEncoder : public FfAudioEncoder
{
public:
	OPUSEncoder(const Properties &properties);
	virtual ~OPUSEncoder() {}

	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetClockRate() { return 48000; }
	virtual bool  GetFmtpInfo(std::string &fmtp, int payloadType);

	// Paramètres fmtp SDP (SANS "a=fmtp:<pt> "), dérivés de la seule config
	// (aucun codec ouvert requis) : forme consommée par la négociation. Le
	// contrôleur SIP préfixe l'en-tête "a=fmtp:<pt> " (décision E, nego_fmtp).
	static std::string GetFmtpParams(const Properties &properties);

	// Ingestion du fmtp distant (phase 5 nego_fmtp, RFC 7587 §7) : les
	// paramètres Opus décrivent ce que leur ÉMETTEUR souhaite RECEVOIR — rien
	// à refléter côté annonce (asymétrique par conception, comme AV1), mais
	// tout à honorer côté émission. `announceProps` reste donc nos propres
	// préférences ; `effectiveProps` reprend celles du pair pour les clés que
	// l'encodeur consomme (useinbandfec, usedtx, maxaveragebitrate, cbr).
	static void ResolveNegotiation(const Properties& localProps,
	                               const std::map<std::string,std::string>& remoteParams,
	                               Properties& announceProps,
	                               Properties& effectiveProps);

private:
	bool useInbandFec;
	bool useDtx;
	int  maxAverageBitrate;
	bool cbr;
	int  packetLossPerc;
};

/**
 * Décodeur OPUS : produit du PCM 48000 Hz. GetRate()/TrySetRate() retournent
 * 48000 statiquement (le décodeur libopus est initialisé à 48000 Hz).
 */
class OPUSDecoder : public FfAudioDecoder
{
public:
	OPUSDecoder();
	virtual ~OPUSDecoder() {}

	virtual DWORD GetRate()         { return 48000; }
	virtual DWORD TrySetRate(DWORD) { return 48000; }

	// OPUS via le wrapper libopus de ffmpeg (cf. en-tête de fichier).
	static bool IsSupported() { return FfAudioDecoder::IsCodecAvailable(AV_CODEC_ID_OPUS, "libopus"); }
};

#endif /* OPUSCODEC_H */
