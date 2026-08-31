/*
 * File:   av1packetizer.cpp
 *
 * Paquetiseur RTP AV1. Voir av1packetizer.h pour la forme retenue (un élément
 * OBU par paquet) et pourquoi.
 */
#include "medkit/log.h"
#include "av1packetizer.h"

bool AV1PacketizeTemporalUnit(const BYTE* data, DWORD len, DWORD maxPayload,
                              std::vector<AV1RtpPacket>& out)
{
	out.clear();

	std::vector<AV1ObuRef> obus;
	if (!AV1ParseObuStream(data, len, obus))
		return false;

	// Le bit N se pose sur le PREMIER paquet de l'unité, il faut donc savoir avant
	// de l'émettre si celle-ci porte un sequence header. C'est la lecture retenue
	// de « first packet of a coded video sequence » : une séquence codée s'ouvre
	// sur un sequence header, et l'encodeur temps réel en émet un avec chaque
	// image clé.
	bool hasSeqHdr = false;
	for (size_t i = 0; i < obus.size(); i++)
		if (obus[i].type == AV1_OBU_SEQUENCE_HEADER)
			hasSeqHdr = true;

	for (size_t i = 0; i < obus.size(); i++)
	{
		const AV1ObuRef& obu = obus[i];

		// Interdit sur le fil, et de toute façon reconstruit à l'autre bout.
		if (obu.type == AV1_OBU_TEMPORAL_DELIMITER)
			continue;

		// Préfixe d'un DÉBUT d'OBU : agrégation + obu_header (+ extension). Les
		// fragments suivants n'ont que l'octet d'agrégation, leurs octets étant la
		// suite brute de la charge.
		const DWORD firstPrefix = 1 + obu.headerLen;

		if (maxPayload <= firstPrefix)
		{
			Log("-AV1PacketizeTemporalUnit: maxPayload %u trop petit pour un prefixe de %u\n",
			    maxPayload, firstPrefix);
			out.clear();
			return false;
		}

		// La tranche part de l'obu_size, qui est TRANSMIS : la longueur d'élément
		// RTP le rend redondant, mais un récepteur qui recolle les charges sans
		// la lire — mediastreamer2 le fait — n'a que lui pour délimiter les OBU
		// d'une même unité.
		const DWORD sizePos   = obu.headerPos + obu.headerLen;
		DWORD       pos       = sizePos;
		DWORD       remaining = obu.payloadLen + (obu.payloadPos - sizePos);
		bool        first     = true;

		// `first ||` : un OBU sans charge (rien que son obu_size) vaut quand même
		// un paquet, sinon il disparaîtrait du flux.
		while (first || remaining > 0)
		{
			const DWORD prefixLen = first ? firstPrefix : 1;
			const DWORD budget    = maxPayload - prefixLen;
			const DWORD take      = (remaining < budget) ? remaining : budget;

			AV1RtpPacket p;
			p.prefixLen = prefixLen;
			p.pos       = pos;
			p.size      = take;
			p.mark      = false;	// posé sur le dernier, après la boucle

			const bool moreOfThisObu = (remaining - take) > 0;

			BYTE agg = 0x10;			// W = 1 : un seul élément, longueur implicite
			if (!first)		agg |= 0x80;	// Z : suite d'un fragment
			if (moreOfThisObu)	agg |= 0x40;	// Y : à suivre dans le paquet suivant
			if (first && out.empty() && hasSeqHdr)
						agg |= 0x08;	// N : début d'une séquence codée

			p.prefix[0] = agg;
			p.prefix[1] = 0;
			p.prefix[2] = 0;

			if (first)
			{
				// L'obu_header part tel quel, obu_has_size_field compris : la taille
				// suit dans la tranche. Aucun octet de la trame n'est réécrit.
				p.prefix[1] = data[obu.headerPos];

				if (obu.headerLen == 2)
					p.prefix[2] = data[obu.headerPos + 1];
			}

			out.push_back(p);

			pos       += take;
			remaining -= take;
			first      = false;
		}
	}

	if (out.empty())
	{
		// Une unité temporelle sans autre OBU que son temporal delimiter : rien à
		// émettre, et rien de bon à en faire.
		Log("-AV1PacketizeTemporalUnit: unite sans OBU transmissible\n");
		return false;
	}

	out.back().mark = true;
	return true;
}
