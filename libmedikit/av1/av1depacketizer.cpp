/*
 * File:   av1depacketizer.cpp
 *
 * Dépaquetiseur RTP AV1 (« RTP Payload Format For AV1 », AOMedia v1.0) vers le
 * format low-overhead bitstream. Voir av1depacketizer.h pour le pourquoi.
 */
#include <string.h>
#include "medkit/log.h"
#include "av1depacketizer.h"

bool AV1ReadLeb128(const BYTE* data, size_t size, size_t& consumed, QWORD& value)
{
	value = 0;
	consumed = 0;

	if (!data)
		return false;

	for (int i = 0; i < 8; i++)
	{
		if (consumed >= size)
			return false;

		const BYTE b = data[consumed];
		value |= (QWORD)(b & 0x7f) << (i * 7);
		consumed++;

		if (!(b & 0x80))
			return true;
	}

	return false; // leb128 trop long (non conforme)
}

DWORD AV1WriteLeb128(BYTE* out, QWORD value)
{
	DWORD len = 0;

	do
	{
		BYTE b = (BYTE)(value & 0x7f);
		value >>= 7;
		if (value)
			b |= 0x80;
		out[len++] = b;
	}
	while (value && len < 8);

	return len;
}

AV1Depacketizer::AV1Depacketizer() :
	fragmentOpen(false), unitHasSeqHdr(false), damaged(false)
{
}

void AV1Depacketizer::ResetFrame()
{
	pending.clear();
	unit.clear();
	fragmentOpen   = false;
	unitHasSeqHdr  = false;
	damaged        = false;
	// seqHdr survit délibérément : il vaut pour toute la séquence codée, et le
	// reconstituer est précisément ce qui permet de repartir après une perte.
}

void AV1Depacketizer::PushObu(const BYTE* obu, DWORD len)
{
	if (!obu || len < 1)
		return;

	const BYTE hdr     = obu[0];
	const int  type    = (hdr >> 3) & 0x0f;
	const bool extFlag = (hdr >> 2) & 0x01;
	const bool hasSize = (hdr >> 1) & 0x01;

	const DWORD headerLen = 1 + (extFlag ? 1 : 0);
	if (len < headerLen)
	{
		damaged = true;
		return;
	}

	const BYTE* payload    = obu + headerLen;
	DWORD       payloadLen = len - headerLen;

	// obu_size sur le fil : la spec RTP recommande son absence (la longueur
	// d'élément le porte déjà) mais ne l'interdit pas. On le retire pour n'avoir
	// qu'UNE forme à réécrire ensuite. La longueur d'élément fait foi ; un
	// obu_size plus petit signale du bourrage, qu'on suit plutôt que de le
	// donner au décodeur.
	if (hasSize)
	{
		size_t consumed = 0;
		QWORD  size     = 0;

		if (!AV1ReadLeb128(payload, payloadLen, consumed, size))
		{
			damaged = true;
			return;
		}

		payload    += consumed;
		payloadLen -= consumed;

		if (size < payloadLen)
			payloadLen = (DWORD)size;
	}

	// Le temporal delimiter NE DOIT PAS être transmis en RTP. S'il arrive quand
	// même, il est redondant avec celui que GetTemporalUnit écrit en tête — et
	// deux TD dans une unité, c'est deux unités pour le décodeur.
	if (type == AV1_OBU_TEMPORAL_DELIMITER)
		return;

	// Réécriture au format low-overhead : obu_has_size_field = 1, suivi du
	// leb128 de la taille. C'est ce qui manquait entièrement — le décodeur
	// recevait des OBU sans délimiteur ET l'octet d'agrégation en tête, qu'il
	// lisait comme un obu_header : « Unknown OBU type 11 ».
	BYTE  head[2 + 8];
	DWORD headLen = 0;

	head[headLen++] = (BYTE)(hdr | 0x02);
	if (extFlag)
		head[headLen++] = obu[1];
	headLen += AV1WriteLeb128(head + headLen, payloadLen);

	unit.insert(unit.end(), head, head + headLen);
	unit.insert(unit.end(), payload, payload + payloadLen);

	if (type == AV1_OBU_SEQUENCE_HEADER)
	{
		// Mis en cache DÉJÀ normalisé : GetTemporalUnit n'a plus qu'à le
		// concaténer.
		seqHdr.assign(head, head + headLen);
		seqHdr.insert(seqHdr.end(), payload, payload + payloadLen);
		unitHasSeqHdr = true;
	}
}

bool AV1Depacketizer::AddPayload(const BYTE* payload, DWORD len, bool lost)
{
	// Une perte casse le réassemblage : impossible de savoir ce qui manquait, ni
	// de refermer un fragment ouvert. L'unité est perdue, la séquence non.
	if (lost)
	{
		damaged = true;
		pending.clear();
		fragmentOpen = false;
	}

	// Charge vide : c'est l'appel de vidange (DecodePacket(NULL,0,1,1)), rien à
	// ingérer.
	if (!payload || len < 1)
		return !damaged;

	const BYTE agg = payload[0];
	const bool Z   = (agg & 0x80) != 0;	// le 1er élément continue un fragment
	const bool Y   = (agg & 0x40) != 0;	// le dernier élément se poursuit
	const int  W   = (agg >> 4) & 0x03;	// nombre d'éléments, 0 = tous préfixés
	const bool N   = (agg & 0x08) != 0;	// début d'une nouvelle séquence codée

	// N : nouvelle séquence codée. Rien d'avant ne la concerne, et la spec
	// interdit qu'un tel paquet continue un fragment.
	if (N)
	{
		pending.clear();
		fragmentOpen = false;
	}

	// Un fragment annoncé comme suite alors qu'on n'a pas sa tête (elle est
	// perdue, ou on prend le flux en cours) : le premier élément est
	// inexploitable, mais les suivants restent bons.
	const bool orphanContinuation = Z && !fragmentOpen;

	// Symétrique : un fragment resté ouvert que ce paquet ne continue pas.
	if (!Z && fragmentOpen)
	{
		damaged = true;
		pending.clear();
		fragmentOpen = false;
	}

	if (orphanContinuation)
	{
		damaged = true;
		pending.clear();
	}

	DWORD pos   = 1;
	int   index = 0;

	while (pos < len)
	{
		QWORD elemLen = 0;

		if (W != 0 && index == W - 1)
		{
			// Dernier élément d'un compte annoncé : longueur implicite.
			elemLen = len - pos;
		}
		else
		{
			size_t consumed = 0;
			if (!AV1ReadLeb128(payload + pos, len - pos, consumed, elemLen))
			{
				Log("-AV1Depacketizer: longueur d'element invalide, unite abandonnee\n");
				damaged = true;
				pending.clear();
				fragmentOpen = false;
				return false;
			}
			pos += consumed;
		}

		if (elemLen == 0 || elemLen > len - pos)
		{
			Log("-AV1Depacketizer: element hors charge [%llu > %u], unite abandonnee\n",
			    (unsigned long long)elemLen, len - pos);
			damaged = true;
			pending.clear();
			fragmentOpen = false;
			return false;
		}

		const bool isFirst = (index == 0);
		const bool isLast  = (W != 0) ? (index == W - 1)
		                              : (pos + elemLen == len);

		// Le seul élément qu'on jette est la suite orpheline : les autres
		// démarrent proprement, y compris dans un paquet endommagé.
		const bool skip = isFirst && orphanContinuation;

		// Borne de sûreté : l'accumulation est pilotée par le réseau.
		if (!skip && unit.size() + pending.size() + elemLen > MaxUnitSize)
		{
			Log("-AV1Depacketizer: unite temporelle au-dela de %u octets, abandonnee\n",
			    MaxUnitSize);
			damaged = true;
			pending.clear();
			fragmentOpen = false;
			return false;
		}

		if (!skip)
		{
			// `pending` porte l'OBU en construction : soit vide (nouvel OBU),
			// soit le début reçu au paquet précédent (isFirst && Z).
			pending.insert(pending.end(), payload + pos, payload + pos + elemLen);

			if (isLast && Y)
				// Il continue dans le paquet suivant : on garde tout en attente.
				fragmentOpen = true;
			else
			{
				PushObu(pending.data(), (DWORD)pending.size());
				pending.clear();
				fragmentOpen = false;
			}
		}

		pos += (DWORD)elemLen;
		index++;

		if (W != 0 && index >= W)
			break;
	}

	// Compte annoncé mais charge trop courte pour le tenir.
	if (W != 0 && index < W)
	{
		Log("-AV1Depacketizer: %d elements annonces, %d lus\n", W, index);
		damaged = true;
		pending.clear();
		fragmentOpen = false;
		return false;
	}

	return !damaged;
}

const BYTE* AV1Depacketizer::GetTemporalUnit(DWORD& len)
{
	out.clear();
	len = 0;

	// Bit marqueur alors qu'un fragment reste ouvert : la fin de l'unité est
	// dans un paquet qui n'est jamais venu.
	if (fragmentOpen)
		damaged = true;

	// Sans sequence header, aucun décodeur AV1 ne peut rien faire de ces
	// octets : rendre l'unité quand même ne produirait que le « No sequence
	// header available » en boucle, sans jamais demander l'image clé qui, elle,
	// débloquerait le flux.
	if (damaged || unit.empty() || seqHdr.empty())
	{
		ResetFrame();
		return NULL;
	}

	// Temporal delimiter : obu_type = 2, obu_has_size_field = 1, taille 0. Il
	// n'est pas décoratif — il délimite l'unité temporelle, et libdav1d le
	// réclame (« Failed to parse temporal unit » sans lui).
	static const BYTE td[2] = { 0x12, 0x00 };
	out.insert(out.end(), td, td + 2);

	// Le sequence header n'est envoyé qu'au début d'une séquence codée : le
	// réémettre à chaque unité qui n'en porte pas est légal (il peut ouvrir
	// n'importe quelle unité) et coûte quelques dizaines d'octets par image. En
	// échange, une prise de flux en cours et une perte se rattrapent seules.
	if (!unitHasSeqHdr)
		out.insert(out.end(), seqHdr.begin(), seqHdr.end());

	out.insert(out.end(), unit.begin(), unit.end());

	ResetFrame();

	len = (DWORD)out.size();
	return out.data();
}
