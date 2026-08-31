/*
 * File:   av1packetizer.h
 *
 * Paquetiseur RTP AV1 : unité temporelle au format low-overhead (la sortie de
 * l'encodeur) -> paquets RTP conformes à « RTP Payload Format For AV1 »
 * (AOMedia v1.0). L'inverse d'av1depacketizer.h, et sans état : une unité
 * temporelle est découpée en une passe, rien n'est reporté à la suivante.
 *
 * Un paquet est décrit comme MediaFrame::AddRtpPacket l'attend — un préfixe
 * court à émettre tel quel, suivi d'une TRANCHE du tampon de trame. Rien n'est
 * recopié : c'est ce qui permet à AV1Encoder::PacketizeFrame de ne pas
 * réécrire la trame que ffmpeg vient de produire.
 *
 * UN élément OBU par paquet (W=1), et sa longueur est donc implicite : aucun
 * champ leb128 n'est écrit dans la charge. C'est le choix qui rend la tranche
 * CONTIGUË, donc exprimable en préfixe + offsets. L'agrégation de plusieurs OBU
 * dans un paquet aurait exigé d'intercaler des longueurs entre les données, donc
 * de recopier la trame ; elle n'économiserait qu'un paquet par image clé, la
 * seule qui porte plus d'un OBU utile en temps réel (sequence header + frame).
 */
#ifndef _AV1PACKETIZER_H_
#define _AV1PACKETIZER_H_

#include "av1obu.h"

// Un paquet RTP décrit par un préfixe et une tranche du tampon de trame.
struct AV1RtpPacket
{
	BYTE  prefix[3];	// agrégation, puis obu_header (+ extension) sur un début d'OBU
	DWORD prefixLen;
	DWORD pos;		// début de la tranche dans le tampon de trame
	DWORD size;		// longueur de la tranche
	bool  mark;		// dernier paquet de l'unité temporelle
};

// Découpe une unité temporelle en paquets RTP. `maxPayload` est la taille utile
// maximale d'un paquet, PRÉFIXE COMPRIS. Rend false si le flux OBU est
// incohérent ou si `maxPayload` est trop petit pour porter le moindre octet
// utile — l'appelant retombe alors sur sa paquetisation générique.
//
// Ce qui est retiré, ajouté ou réécrit au passage :
//   - le temporal delimiter est SUPPRIMÉ (la spec RTP l'interdit sur le fil) ;
//   - obu_has_size_field et obu_size sont transmis TELS QUELS, alors que la
//     spec recommande leur absence (la longueur de l'élément porte déjà la
//     taille). Elle l'autorise, et c'est ce qui rend le flux lisible par un
//     récepteur qui recolle les charges sans lire les bits d'agrégation :
//     mediastreamer2 (Linphone) fait exactement cela, et sans obu_size il ne
//     peut pas séparer le sequence header de la trame dans une image clé ;
//   - le bit N est posé sur le premier paquet d'une unité qui contient un
//     sequence header, ce qui est le début d'une séquence codée.
bool AV1PacketizeTemporalUnit(const BYTE* data, DWORD len, DWORD maxPayload,
                              std::vector<AV1RtpPacket>& out);

#endif /* _AV1PACKETIZER_H_ */
