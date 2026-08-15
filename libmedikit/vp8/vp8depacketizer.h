/*
 * File:   vp8depacketizer.h
 *
 * Dépaquetiseur RTP VP8 (RFC 7741) : réassemble les payloads RTP en une trame
 * VP8 complète, en retirant le payload descriptor en tête de chaque paquet.
 *
 * Contrairement à H.264, il n'y a rien à réécrire : la trame est la simple
 * concaténation des payloads débarrassés de leur descripteur. Le travail utile
 * est ailleurs : reconnaître le début de trame (S=1, PID=0), lire le bit
 * frame_type du payload header VP8 pour marquer les trames clés — c'est ce
 * drapeau intra que waitVideo (mp4writer) et la création de piste attendent —
 * et extraire largeur/hauteur du keyframe header non compressé.
 *
 * Ne dépend PAS de ffmpeg : manipulation d'octets pure, testable seule
 * (tests/test_vp8_depacketizer.cpp). VP8Decoder::DecodePacket utilise le même
 * parseur de descripteur (VP8DescriptorLen), en un seul exemplaire.
 */
#ifndef VP8DEPACKETIZER_H
#define VP8DEPACKETIZER_H

#include "../medkit/video.h"

// Longueur (octets) du VP8 payload descriptor (RFC 7741) en tête d'un payload
// RTP ; 0 si le payload est trop court pour porter le descripteur qu'il
// annonce (paquet invalide).
DWORD VP8DescriptorLen(const BYTE* data, DWORD size);

class VP8Depacketizer
{
public:
	// Borne de SÛRETÉ sur la trame réassemblée, pas un réglage :
	// l'accumulation est pilotée par le réseau et rien n'oblige un émetteur
	// cassé (ou hostile) à poser le mark RTP — sans borne, le tampon grossit
	// jusqu'à épuisement mémoire (même motif que AV1Depacketizer::MaxUnitSize).
	static const DWORD MaxFrameSize = 4 * 1024 * 1024;

	VP8Depacketizer();
	virtual ~VP8Depacketizer();
	virtual void SetTimestamp(DWORD timestamp);
	virtual MediaFrame* AddPayload(BYTE* payload, DWORD payload_len, bool mark);
	virtual void ResetFrame();

private:
	VideoFrame frame;
	// On a vu le paquet de tête (S=1, PID=0) de la trame en cours : sans lui
	// (arrivée en cours de flux, perte), la trame est indécodable et rien
	// n'est accumulé.
	bool started;
};

#endif	/* VP8DEPACKETIZER_H */
