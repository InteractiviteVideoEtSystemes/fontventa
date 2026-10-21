/*
 * File:   amrpayload.cpp
 *
 * Charge utile RTP AMR / AMR-WB (RFC 4867 §4) <-> format stockage (§5).
 */
#include "amrpayload.h"
#include <vector>
#include <sstream>
#include <string.h>
#include <stdlib.h>

namespace
{
	// Bits de parole par type de trame : AMR-NB FT 0..8 (8 = SID), AMR-WB FT 0..9
	// (9 = SID). FT 15 (NO_DATA), et FT 14 en AMR-WB (SPEECH_LOST), n'en portent
	// aucun.
	const int nbBits[] = { 95, 103, 118, 134, 148, 159, 204, 244, 39 };
	const int wbBits[] = { 132, 177, 253, 285, 317, 365, 397, 461, 477, 40 };

	struct Toc
	{
		BYTE ft;
		bool q;
		int  bits;
	};

	BYTE StorageHeader(const Toc& t) { return (BYTE)((t.ft << 3) | (t.q ? 0x04 : 0)); }

	class BitReader
	{
	public:
		BitReader(const BYTE* data, DWORD len) : data(data), total(len * 8), pos(0) {}
		bool Has(DWORD n) const { return pos + n <= total; }
		DWORD Get(DWORD n)
		{
			DWORD v = 0;
			for (DWORD i = 0; i < n; i++, pos++)
				v = (v << 1) | ((data[pos >> 3] >> (7 - (pos & 7))) & 1);
			return v;
		}
	private:
		const BYTE* data;
		DWORD total;
		DWORD pos;
	};

	class BitWriter
	{
	public:
		BitWriter(BYTE* data, DWORD max) : data(data), total(max * 8), pos(0) {}
		bool Put(DWORD v, DWORD n)
		{
			if (pos + n > total)
				return false;
			for (DWORD i = 0; i < n; i++, pos++)
			{
				BYTE& b = data[pos >> 3];
				if ((pos & 7) == 0)
					b = 0;
				if ((v >> (n - 1 - i)) & 1)
					b |= 0x80 >> (pos & 7);
			}
			return true;
		}
		DWORD Bytes() const { return (pos + 7) / 8; }
	private:
		BYTE* data;
		DWORD total;
		DWORD pos;
	};

	bool MakeToc(bool wideband, BYTE ft, bool q, Toc& t)
	{
		t.ft = ft;
		t.q = q;
		t.bits = AMRPayload::FrameBits(wideband, ft);
		return t.bits >= 0;
	}
}

int AMRPayload::FrameBits(bool wideband, BYTE ft)
{
	if (wideband)
	{
		if (ft < sizeof(wbBits) / sizeof(wbBits[0]))
			return wbBits[ft];
		return (ft == 14 || ft == 15) ? 0 : -1;
	}
	if (ft < sizeof(nbBits) / sizeof(nbBits[0]))
		return nbBits[ft];
	return ft == 15 ? 0 : -1;
}

int AMRPayload::ToStorage(bool wideband, bool octetAligned,
                          const BYTE* in, DWORD len, BYTE* out, DWORD max)
{
	std::vector<Toc> tocs;
	DWORD written = 0;

	if (octetAligned)
	{
		//CMR, puis une entrée de table par octet : F|FT|Q|P P.
		DWORD pos = 1;
		bool more = true;
		while (more)
		{
			if (pos >= len)
				return -1;
			const BYTE b = in[pos++];
			Toc t;
			if (!MakeToc(wideband, (b >> 3) & 0x0F, b & 0x04, t))
				return -1;
			tocs.push_back(t);
			more = b & 0x80;
		}
		//En octet-aligned, chaque trame est complétée à l'octet : c'est déjà
		//le corps d'une trame au format stockage.
		for (const Toc& t : tocs)
		{
			const DWORD n = (t.bits + 7) / 8;
			if (pos + n > len || written + 1 + n > max)
				return -1;
			out[written++] = StorageHeader(t);
			memcpy(out + written, in + pos, n);
			pos += n;
			written += n;
		}
		return written;
	}

	//Bandwidth-efficient : CMR sur 4 bits, entrées de table sur 6 bits
	//(F|FT|Q), puis les bits de parole de chaque trame, sans bourrage entre elles.
	BitReader r(in, len);
	if (!r.Has(4))
		return -1;
	r.Get(4);
	bool more = true;
	while (more)
	{
		if (!r.Has(6))
			return -1;
		more = r.Get(1);
		const BYTE ft = r.Get(4);
		const bool q = r.Get(1);
		Toc t;
		if (!MakeToc(wideband, ft, q, t))
			return -1;
		tocs.push_back(t);
	}
	for (const Toc& t : tocs)
	{
		const DWORD n = (t.bits + 7) / 8;
		if (!r.Has(t.bits) || written + 1 + n > max)
			return -1;
		out[written++] = StorageHeader(t);
		BitWriter w(out + written, n);
		for (int left = t.bits; left > 0; left -= 8)
		{
			const DWORD chunk = left < 8 ? left : 8;
			w.Put(r.Get(chunk), chunk);
		}
		written += n;
	}
	return written;
}

int AMRPayload::FromStorage(bool wideband, bool octetAligned,
                            const BYTE* in, DWORD len, BYTE* out, DWORD max)
{
	std::vector<Toc> tocs;
	std::vector<const BYTE*> bodies;
	for (DWORD pos = 0; pos < len; )
	{
		const BYTE h = in[pos++];
		Toc t;
		if (!MakeToc(wideband, (h >> 3) & 0x0F, h & 0x04, t))
			return -1;
		const DWORD n = (t.bits + 7) / 8;
		if (pos + n > len)
			return -1;
		tocs.push_back(t);
		bodies.push_back(in + pos);
		pos += n;
	}
	if (tocs.empty())
		return -1;

	if (octetAligned)
	{
		DWORD need = 1 + tocs.size();
		for (const Toc& t : tocs)
			need += (t.bits + 7) / 8;
		if (need > max)
			return -1;

		DWORD written = 0;
		out[written++] = 0xF0;
		for (size_t i = 0; i < tocs.size(); i++)
			out[written++] = StorageHeader(tocs[i]) | (i + 1 < tocs.size() ? 0x80 : 0);
		for (size_t i = 0; i < tocs.size(); i++)
		{
			const DWORD n = (tocs[i].bits + 7) / 8;
			memcpy(out + written, bodies[i], n);
			written += n;
		}
		return written;
	}

	BitWriter w(out, max);
	if (!w.Put(15, 4))
		return -1;
	for (size_t i = 0; i < tocs.size(); i++)
	{
		const DWORD entry = ((i + 1 < tocs.size() ? 1 : 0) << 5) | (tocs[i].ft << 1) | (tocs[i].q ? 1 : 0);
		if (!w.Put(entry, 6))
			return -1;
	}
	for (size_t i = 0; i < tocs.size(); i++)
	{
		BitReader r(bodies[i], (tocs[i].bits + 7) / 8);
		for (int left = tocs[i].bits; left > 0; left -= 8)
		{
			const DWORD chunk = left < 8 ? left : 8;
			if (!w.Put(r.Get(chunk), chunk))
				return -1;
		}
	}
	return w.Bytes();
}

bool AMRPayload::OctetAligned(const std::map<std::string,std::string>& fmtp)
{
	std::map<std::string,std::string>::const_iterator it = fmtp.find("octet-align");
	return it != fmtp.end() && atoi(it->second.c_str()) == 1;
}

bool AMRPayload::Negotiate(bool wideband, const std::map<std::string,std::string>& remote,
                           std::string& fmtp, int& mode)
{
	const int highest = wideband ? 8 : 7;

	for (const char* unsupported : { "crc", "robust-sorting" })
	{
		std::map<std::string,std::string>::const_iterator it = remote.find(unsupported);
		if (it != remote.end() && atoi(it->second.c_str()) != 0)
			return false;
	}
	if (remote.count("interleaving"))
		return false;

	std::ostringstream out;
	if (OctetAligned(remote))
		out << "octet-align=1";

	mode = highest;
	std::map<std::string,std::string>::const_iterator set = remote.find("mode-set");
	if (set != remote.end())
	{
		std::ostringstream modes;
		mode = -1;
		std::istringstream in(set->second);
		std::string item;
		while (std::getline(in, item, ','))
		{
			char* end = NULL;
			const long m = strtol(item.c_str(), &end, 10);
			if (end == item.c_str() || m < 0 || m > highest)
				continue;
			modes << (mode < 0 ? "" : ",") << m;
			if (m > mode)
				mode = (int)m;
		}
		if (mode < 0)
			return false;
		out << (out.tellp() > 0 ? ";" : "") << "mode-set=" << modes.str();
	}

	fmtp = out.str();
	return true;
}
