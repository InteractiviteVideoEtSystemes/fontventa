/*
 * File:   vp8frameheader.cpp
 *
 * Voir vp8frameheader.h. Références :
 *  - RFC 6386 §7.3 : décodeur booléen ;
 *  - RFC 6386 §9.1-9.7 : ordre des champs du frame header. L'ordre du §9.7
 *    est : les DEUX drapeaux refresh d'abord, puis les codes de copie
 *    conditionnels (vérifié sur le texte de la RFC, pas de mémoire).
 */
#include "vp8frameheader.h"

namespace
{

// Décodeur booléen du §7.3, borné à la première partition : lire au-delà rend
// des zéros et arme overrun — le parseur refuse alors la trame plutôt que
// d'inventer des drapeaux.
class BoolDecoder
{
public:
	BoolDecoder(const BYTE* data, DWORD size) : input(data), size(size)
	{
		value = (DWORD(NextByte()) << 8) | NextByte();
	}

	// Tous les champs du frame header se lisent à probabilité uniforme (128).
	int Bit()
	{
		DWORD split = 1 + (((range - 1) * 128) >> 8);
		DWORD scaled = split << 8;
		int ret;
		if (value >= scaled)
		{
			ret = 1;
			range -= split;
			value -= scaled;
		}
		else
		{
			ret = 0;
			range = split;
		}
		while (range < 128)
		{
			value <<= 1;
			range <<= 1;
			if (++bitCount == 8)
			{
				bitCount = 0;
				value |= NextByte();
			}
		}
		return ret;
	}

	DWORD Literal(int bits)
	{
		DWORD v = 0;
		while (bits--)
			v = (v << 1) | Bit();
		return v;
	}

	// Motif récurrent « drapeau + magnitude + signe » (§9.3, §9.4, §9.6)
	void SkipMaybeSigned(int bits)
	{
		if (Bit())
		{
			Literal(bits);
			Bit();
		}
	}

	bool Overrun() const { return overrun; }

private:
	BYTE NextByte()
	{
		if (pos < size)
			return input[pos++];
		overrun = true;
		return 0;
	}

	const BYTE* input;
	DWORD size;
	DWORD pos = 0;
	DWORD range = 255;
	DWORD value = 0;
	int bitCount = 0;
	bool overrun = false;
};

}

bool VP8ParseFrameHeader(const BYTE* data, DWORD size, VP8FrameHeaderInfo &info)
{
	info = VP8FrameHeaderInfo();

	// §9.1 : frame tag non compressé, 3 octets little-endian
	if (!data || size < 3)
		return false;
	DWORD tag = data[0] | (DWORD(data[1]) << 8) | (DWORD(data[2]) << 16);
	info.keyFrame  = !(tag & 0x01);
	info.showFrame = (tag >> 4) & 0x01;
	DWORD firstPartSize = (tag >> 5) & 0x7FFFF;

	if (info.keyFrame)
	{
		// §9.1 : start code et dimensions ; §9.7 : golden et altref sont
		// remplacées d'office — aucun bit à lire.
		if (size < 10 || data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a)
			return false;
		info.refreshGolden = true;
		info.refreshAltRef = true;
		return true;
	}

	// La première partition compressée suit le tag
	if (!firstPartSize || firstPartSize > size - 3)
		return false;
	BoolDecoder bc(data + 3, firstPartSize);

	// §9.3 Segmentation
	if (bc.Bit())	// segmentation_enabled
	{
		int updateMap  = bc.Bit();
		int updateData = bc.Bit();
		if (updateData)
		{
			bc.Bit();	// segment_feature_mode
			for (int i = 0; i < 4; i++)
				bc.SkipMaybeSigned(7);	// quantizer par segment
			for (int i = 0; i < 4; i++)
				bc.SkipMaybeSigned(6);	// loop filter par segment
		}
		if (updateMap)
			for (int i = 0; i < 3; i++)
				if (bc.Bit())
					bc.Literal(8);	// tree probs
	}

	// §9.4 Loop filter
	bc.Bit();	// filter_type
	bc.Literal(6);	// loop_filter_level
	bc.Literal(3);	// sharpness_level
	if (bc.Bit())	// loop_filter_adj_enable
		if (bc.Bit())	// mode_ref_lf_delta_update
		{
			for (int i = 0; i < 4; i++)
				bc.SkipMaybeSigned(6);	// ref frame deltas
			for (int i = 0; i < 4; i++)
				bc.SkipMaybeSigned(6);	// mb mode deltas
		}

	// §9.5 : nombre de partitions de tokens (leurs TAILLES sont hors du flux
	// booléen, après la première partition — pas à sauter ici)
	bc.Literal(2);

	// §9.6 Indices de quantification
	bc.Literal(7);	// y_ac_qi
	for (int i = 0; i < 5; i++)
		bc.SkipMaybeSigned(4);	// y1dc, y2dc, y2ac, uvdc, uvac

	// §9.7 Mise à jour des références
	info.refreshGolden = bc.Bit();
	info.refreshAltRef = bc.Bit();
	if (!info.refreshGolden)
		info.copyToGolden = bc.Literal(2);
	if (!info.refreshAltRef)
		info.copyToAltRef = bc.Literal(2);

	return !bc.Overrun();
}
