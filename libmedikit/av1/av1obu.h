/*
 * File:   av1obu.h
 *
 * Boîte à outils OBU commune aux deux sens du transport AV1 : leb128, types
 * d'OBU, et parcours d'un flux « low overhead bitstream » (AV1 spec §5.2, celui
 * que produit l'encodeur et que consomme le décodeur).
 *
 * Les deux sens sont dans des fichiers séparés parce qu'ils n'ont rien en
 * commun au-delà de ça : av1depacketizer.h (RTP → OBU) porte de l'état
 * — fragments à recoller, sequence header en cache — tandis que
 * av1packetizer.h (OBU → RTP) est sans état, une unité temporelle à la fois.
 * Ce qu'ils partagent est ici, en un seul exemplaire : une lecture leb128
 * divergente entre les deux ne se remarquerait que sur les tailles ≥ 128, donc
 * jamais sur un flux de test.
 */
#ifndef _AV1OBU_H_
#define _AV1OBU_H_

#include "../medkit/config.h"
#include <vector>

// Types d'OBU dont ce code a besoin (AV1 spec §6.2.2). La liste complète n'a pas
// d'intérêt : seuls ceux-là se traitent à part.
enum
{
	AV1_OBU_SEQUENCE_HEADER    = 1,
	AV1_OBU_TEMPORAL_DELIMITER = 2,
};

// leb128 (AV1 spec §4.10.5) : le format de longueur des OBU comme des éléments
// RTP. Rend false sur un leb128 tronqué ou non conforme (> 8 octets).
bool AV1ReadLeb128(const BYTE* data, size_t size, size_t& consumed, QWORD& value);

// Écrit `value` en leb128 dans `out` (au plus 8 octets) ; rend le nombre
// d'octets écrits. Forme minimale, comme l'exige la spec.
DWORD AV1WriteLeb128(BYTE* out, QWORD value);

// Un OBU repéré dans un flux low-overhead, décrit par des offsets : rien n'est
// recopié, l'appelant garde le tampon.
struct AV1ObuRef
{
	int   type;
	DWORD headerPos;	// début de l'OBU (obu_header)
	DWORD headerLen;	// 1, ou 2 avec l'octet d'extension
	DWORD payloadPos;	// après obu_size
	DWORD payloadLen;	// tel que déclaré par obu_size
};

// Parcourt un flux d'OBU au format low-overhead — chaque OBU portant son
// obu_size en leb128, ce qui est ce que produit ffmpeg. Rend false dès que la
// chaîne est incohérente (OBU sans champ de taille, taille hors tampon) : rien
// n'est alors exploitable, car la position de l'OBU suivant dépend de la taille
// du précédent.
bool AV1ParseObuStream(const BYTE* data, DWORD len, std::vector<AV1ObuRef>& out);

#endif /* _AV1OBU_H_ */
