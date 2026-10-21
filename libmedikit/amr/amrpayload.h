/*
 * File:   amrpayload.h
 *
 * Charge utile RTP de l'AMR et de l'AMR-WB (RFC 4867 §4), dans ses deux modes :
 * octet-aligned (`octet-align=1`) et bandwidth-efficient, le mode par défaut
 * quand le fmtp ne dit rien (§8.1).
 *
 * Les codecs (AMRNBEncoder/AMRWBEncoder et leurs décodeurs, sur ffmpeg) lisent et
 * écrivent le FORMAT STOCKAGE (§5) : chaque trame précédée de son octet d'en-tête
 * 0|FT|Q|00. C'est aussi ce que portent les fichiers 3GP/MP4. Le fil, lui, porte un
 * CMR puis une table des matières, et les trames y sont mises bout à bout. Ces
 * deux fonctions traduisent l'un en l'autre, au bord du fil.
 *
 * Seul le format simple est porté : ni CRC, ni tri robuste, ni entrelacement
 * (§4.3.2, §4.4.1). Le négociateur refuse un PT qui les demande. Le CMR reçu est
 * ignoré, celui émis vaut 15 (« pas de demande »).
 *
 * Ne dépend PAS de ffmpeg : manipulation d'octets pure (tests/test_amr_payload.cpp).
 */
#ifndef AMRPAYLOAD_H
#define AMRPAYLOAD_H

#include "../medkit/config.h"
#include <map>
#include <string>

namespace AMRPayload
{
	// Nombre de bits de parole d'une trame de type `ft` (RFC 4867 §3.6 et
	// TS 26.101 / 26.201), 0 pour NO_DATA et SPEECH_LOST, -1 pour un type que
	// nous ne savons pas porter (réservé, ou SID d'un autre codec en AMR-NB).
	int FrameBits(bool wideband, BYTE ft);

	// Charge utile RTP -> trames au format stockage, mises bout à bout.
	// Rend la longueur écrite dans `out`, ou -1 si la charge est invalide ou ne
	// tient pas dans `max`.
	int ToStorage(bool wideband, bool octetAligned,
	              const BYTE* in, DWORD len, BYTE* out, DWORD max);

	// Trames au format stockage, mises bout à bout -> charge utile RTP.
	// Mêmes conventions de retour.
	int FromStorage(bool wideband, bool octetAligned,
	                const BYTE* in, DWORD len, BYTE* out, DWORD max);

	// Le mode d'un PT, lu sur son fmtp (paramètres seuls, clés en minuscules).
	bool OctetAligned(const std::map<std::string,std::string>& fmtp);

	// Négociation d'un PT AMR (RFC 4867 §8.3). Faux si le pair demande ce que nous
	// ne portons pas (`crc`, `robust-sorting`, `interleaving`), ou un `mode-set`
	// sans aucun mode de parole valide : le PT ne doit pas être accepté. Sinon,
	// `fmtp` reçoit ce que nous répondons — `octet-align` doit être identique des
	// deux côtés, et le `mode-set` du pair est repris —, et `mode` le mode le plus
	// haut que l'encodeur a le droit d'émettre.
	bool Negotiate(bool wideband, const std::map<std::string,std::string>& remote,
	               std::string& fmtp, int& mode);
}

#endif	/* AMRPAYLOAD_H */
