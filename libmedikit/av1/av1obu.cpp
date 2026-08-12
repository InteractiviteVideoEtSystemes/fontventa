/*
 * File:   av1obu.cpp
 *
 * leb128 et parcours d'un flux OBU low-overhead. Voir av1obu.h.
 */
#include "medkit/log.h"
#include "av1obu.h"

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

bool AV1ParseObuStream(const BYTE* data, DWORD len, std::vector<AV1ObuRef>& out)
{
	out.clear();

	if (!data)
		return false;

	DWORD pos = 0;

	while (pos < len)
	{
		const BYTE hdr     = data[pos];
		const bool extFlag = (hdr >> 2) & 0x01;
		const bool hasSize = (hdr >> 1) & 0x01;

		AV1ObuRef obu;
		obu.type      = (hdr >> 3) & 0x0f;
		obu.headerPos = pos;
		obu.headerLen = 1 + (extFlag ? 1 : 0);

		if (pos + obu.headerLen > len)
			return false;

		if (!hasSize)
		{
			// Sans obu_size, la fin de cet OBU — donc le début du suivant — est
			// indéterminée. C'est le format low-overhead qui est attendu ici.
			Log("-AV1ParseObuStream: OBU sans champ de taille\n");
			return false;
		}

		size_t consumed = 0;
		QWORD  size     = 0;
		if (!AV1ReadLeb128(data + pos + obu.headerLen, len - pos - obu.headerLen,
		                   consumed, size))
			return false;

		obu.payloadPos = pos + obu.headerLen + (DWORD)consumed;

		if (obu.payloadPos + size > len)
			return false;

		obu.payloadLen = (DWORD)size;
		out.push_back(obu);

		pos = obu.payloadPos + obu.payloadLen;
	}

	return true;
}
