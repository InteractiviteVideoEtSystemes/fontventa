/*
 * File:   av1codec.h
 *
 * Encodeur/décodeur AV1 sur ffmpeg. Backend forcé "libsvtav1" côté encodeur
 * (temps-réel capable ; avcodec_find_encoder(AV_CODEC_ID_AV1) renvoie par
 * défaut "libaom-av1", bien plus lent) et "libdav1d" côté décodeur (déjà le
 * choix par défaut de ffmpeg pour AV1, forcé ici par explicité plutôt que par
 * dépendance à l'ordre de résolution interne de ffmpeg).
 *
 * Les deux sens du transport RTP vivent à côté, chacun testable seul et sans
 * dépendance ffmpeg — c'est ce codec qui les emploie :
 *   - av1depacketizer.h : RTP -> OBU, appelé par AV1Decoder::DecodePacket. Son
 *     absence rendait tout décodage AV1 impossible jusqu'au 2026-08-12 :
 *     l'accumulation brute héritée donnait à libdav1d l'octet d'agrégation
 *     comme s'il était un obu_header.
 *   - av1packetizer.h : OBU -> RTP, appelé par AV1Encoder::PacketizeFrame. Ne
 *     concerne que l'AV1 que le serveur encode LUI-MÊME (transcodage vers AV1,
 *     mixage, lecture de fichier) ; un pont AV1 <-> AV1 relaie les paquets du
 *     pair sans traverser ni l'un ni l'autre.
 */
#ifndef _AV1CODEC_H_
#define _AV1CODEC_H_

#include "../ffvideocodec.h"
#include "av1depacketizer.h"
#include "av1packetizer.h"
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

	// Reconfiguration à chaud : le wrapper SVT-AV1 de ffmpeg ne relit pas
	// ctx->bit_rate en cours de route (seul libx264 le fait par trame), donc la
	// mémorisation de FfVideoEncoder::SetFrameRate ne suffit pas. Réouverture
	// (=> nouvelle trame clé) sur variation >=10 %, comme H264 en VAAPI.
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod);

protected:
	virtual void ConfigureContext();
	virtual void PacketizeFrame();

private:
	int  preset;	// av1.preset : SVT-AV1 -1..13 (plus petit = plus lent/meilleur)
	// Débit d'ouverture, pour la logique de réouverture de SetFrameRate.
	int  openedBitrate;

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

	// Dépaquetisation RTP AV1 (spec AV1 RTP payload format) : réassemble les
	// OBU fragmentés, rétablit obu_size et le temporal delimiter, puis décode
	// l'unité temporelle sur le bit marqueur. Rend 0 quand rien n'est
	// décodable — c'est le signal qui fait demander une image clé à l'appelant.
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);

	static bool IsSupported() { return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_AV1, "libdav1d"); }

	// Pas de GetFmtpInfo ici, même raison que H264Decoder : un décodeur
	// n'origine pas de sequence header, il réassemble celui qu'il reçoit ; le
	// fmtp se construit côté AV1Encoder, sur le flux qu'il produit lui-même.

private:
	AV1Depacketizer	depacketizer;
};

#endif
