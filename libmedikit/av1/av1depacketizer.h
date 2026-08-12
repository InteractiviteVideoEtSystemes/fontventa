/*
 * File:   av1depacketizer.h
 *
 * Dépaquetiseur RTP AV1 : charges RTP -> unité temporelle au format « low
 * overhead bitstream » (AV1 spec §5.2), le seul que libdav1d/ffmpeg sait lire
 * quand on lui donne les octets directement.
 *
 * La spec de référence est « RTP Payload Format For AV1 » (AOMedia v1.0). Ce
 * qui la distingue de RFC 6184 (H.264) tient en trois points, et ce sont les
 * trois seules choses que cette classe fait :
 *
 *  1. UN octet d'agrégation en tête de charge (Z/Y/W/N) — pas un descripteur
 *     par NALU. Les OBU du paquet suivent, chacun précédé de sa longueur en
 *     leb128, sauf le dernier quand W != 0 (longueur implicite = reste).
 *  2. Un OBU peut être FRAGMENTÉ sur plusieurs paquets : Y=1 dit « le dernier
 *     élément continue », Z=1 dit « le premier élément est cette suite ». Le
 *     réassemblage est donc à état, contrairement à VP8.
 *  3. Le temporal delimiter OBU NE DOIT PAS être transmis, et l'`obu_size` est
 *     redondant sur le fil (la longueur d'élément le porte) donc absent : les
 *     deux doivent être RÉTABLIS pour que le décodeur puisse délimiter les OBU.
 *
 * Le sequence header est mis en cache et réémis en tête de chaque unité qui
 * n'en porte pas : AV1/RTP ne le répète pas, alors qu'un pont média voit des
 * arrivées en cours de flux et des pertes. Sans cela, un décodeur qui a manqué
 * le paquet N=1 ne décode plus JAMAIS rien — « No sequence header available »,
 * exactement la trace du 2026-08-12.
 *
 * Ne dépend PAS de ffmpeg : c'est de la manipulation d'octets, testable seule
 * (tests/test_av1_depacketizer.cpp). AV1Decoder::DecodePacket n'en est que
 * l'utilisateur.
 *
 * Le PAQUETISEUR symétrique reste à écrire (cf. AV1Encoder::PacketizeFrame,
 * qui retombe encore sur le découpage générique de FfVideoEncoder) : il n'est
 * nécessaire que là où le serveur ÉMET de l'AV1 qu'il a lui-même encodé.
 */
#ifndef _AV1DEPACKETIZER_H_
#define _AV1DEPACKETIZER_H_

// Types d'OBU, leb128 et parcours d'un flux low-overhead : la boîte à outils
// partagée avec le paquetiseur (av1packetizer.h), en un seul exemplaire.
#include "av1obu.h"

class AV1Depacketizer
{
public:
	// Taille maximale d'une unité temporelle réassemblée, et d'un OBU en cours de
	// fragmentation. C'est une borne de SÛRETÉ, pas un réglage : l'accumulation
	// est pilotée par le réseau, et rien dans le format n'oblige un pair à poser
	// le bit marqueur — sans borne, un émetteur cassé (ou hostile) fait grossir
	// le tampon jusqu'à épuisement mémoire. 4 Mo laisse passer une image clé
	// haute définition très large ; le décodeur, lui, refuse déjà au-delà de son
	// propre tampon (~1,1 Mo).
	static const DWORD MaxUnitSize = 4 * 1024 * 1024;

	AV1Depacketizer();

	// Abandonne l'unité temporelle en cours et l'état de fragmentation. Le
	// sequence header en cache SURVIT : c'est tout l'intérêt de le cacher.
	void ResetFrame();

	// Ingère une charge RTP (sans l'en-tête RTP). `lost` : au moins un paquet
	// a été perdu juste avant celui-ci. Rend false si la charge est
	// inexploitable ou si l'unité en cours est désormais incomplète — dans les
	// deux cas l'unité est abandonnée et l'appelant devrait demander une image
	// clé (FPU).
	bool AddPayload(const BYTE* payload, DWORD len, bool lost);

	// Unité temporelle complète, à donner telle quelle au décodeur, quand le
	// bit marqueur RTP est vu. Vide s'il n'y a rien de décodable (unité
	// endommagée, ou aucun OBU utile). Invalide dès le prochain AddPayload.
	const BYTE* GetTemporalUnit(DWORD& len);

	// L'unité en cours a-t-elle perdu des octets ? Un appelant qui préfère ne
	// rien décoder plutôt que du partiel peut le lire avant GetTemporalUnit.
	bool IsDamaged() const { return damaged; }

	// Un sequence header a-t-il déjà été vu ? Tant que non, aucune unité n'est
	// décodable et la seule issue est une image clé.
	bool HasSequenceHeader() const { return !seqHdr.empty(); }

private:
	// Ajoute un OBU complet (en-tête + charge, tel que reçu, sans obu_size) à
	// l'unité en cours : rétablit obu_has_size_field et le leb128 de taille,
	// filtre le temporal delimiter, capture le sequence header.
	void PushObu(const BYTE* obu, DWORD len);

	// L'OBU en cours de fragmentation, accumulé d'un paquet au suivant.
	std::vector<BYTE> pending;
	// L'unité temporelle en construction, déjà au format low-overhead.
	std::vector<BYTE> unit;
	// Dernier sequence header OBU vu, déjà normalisé (prêt à être concaténé).
	std::vector<BYTE> seqHdr;
	// Rendu par GetTemporalUnit : unité complète, préfixée du temporal
	// delimiter et du sequence header si besoin.
	std::vector<BYTE> out;

	// Un OBU est ouvert : le prochain paquet doit commencer par Z=1.
	bool fragmentOpen;
	// L'unité en cours porte déjà un sequence header.
	bool unitHasSeqHdr;
	// Des octets manquent dans l'unité en cours.
	bool damaged;
};

#endif /* _AV1DEPACKETIZER_H_ */
