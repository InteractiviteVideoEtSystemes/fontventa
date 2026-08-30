/*
 * File:   vp8depacketizer.cpp
 *
 * Voir vp8depacketizer.h. Références :
 *  - RFC 7741 (RTP Payload Format for VP8) : payload descriptor ;
 *  - RFC 6386 §9.1 (VP8 Data Format) : payload header (frame tag 3 octets
 *    little-endian) et keyframe header (start code 9d 01 2a, dimensions).
 */
#include "vp8depacketizer.h"
#include "../medkit/log.h"

/***********************
* VP8DescriptorLen
*	Longueur (octets) du VP8 payload descriptor (RFC 7741) à retirer en tête de
*	payload RTP. Porté depuis mcu/src/vp8/vp8.h (VP8PayloadDescriptor::Parse).
************************/
DWORD VP8DescriptorLen(const BYTE* data, DWORD size)
{
	if (size < 1)
		return 0;

	DWORD len = 1;
	/*  0 1 2 3 4 5 6 7
	 * +-+-+-+-+-+-+-+-+
	 * |X|R|N|S|R| PID |
	 * +-+-+-+-+-+-+-+-+ */
	bool X = data[0] >> 7;
	if (X)
	{
		if (size < 2) return 0;
		/* X: |I|L|T|K|RSV-A| */
		bool I = data[1] >> 7;
		bool L = (data[1] >> 6) & 0x01;
		bool T = (data[1] >> 5) & 0x01;
		bool K = (data[1] >> 4) & 0x01;
		len++; // second octet

		if (I)
		{
			if (len >= size) return 0;
			// PictureID : 1 ou 2 octets (bit M)
			len += (data[len] & 0x80) ? 2 : 1;
		}
		if (L)
			len++;		// TL0PICIDX
		if (T || K)
			len++;		// TID/Y/KEYIDX
	}

	return (len <= size) ? len : 0;
}

bool VP8DescriptorPictureId(const BYTE* data, DWORD size, WORD &pictureId)
{
	if (size < 3)
		return false;
	if (!(data[0] >> 7))	// X=0 : pas d'octet d'extension
		return false;
	if (!(data[1] >> 7))	// I=0 : pas de PictureID
		return false;
	// Le PictureID suit immédiatement l'octet d'extension
	if (data[2] & 0x80)
	{
		// M=1 : 15 bits sur 2 octets réseau, rendus tels quels bit M compris
		if (size < 4)
			return false;
		pictureId = (WORD(data[2]) << 8) | data[3];
	}
	else
		pictureId = data[2];
	return true;
}

VP8Depacketizer::VP8Depacketizer() : frame(VideoCodec::VP8, 0)
{
	started = false;
	frame.SetIntra(false);
}

VP8Depacketizer::~VP8Depacketizer()
{
}

void VP8Depacketizer::SetTimestamp(DWORD timestamp)
{
	frame.SetTimestamp(timestamp);
}

void VP8Depacketizer::ResetFrame()
{
	frame.ClearRTPPacketizationInfo();
	memset(frame.GetData(), 0, frame.GetMaxMediaLength());
	frame.SetLength(0);
	frame.SetIntra(false);
	frame.SetWidth(0);
	frame.SetHeight(0);
	started = false;
}

MediaFrame* VP8Depacketizer::AddPayload(BYTE* payload, DWORD payload_len, bool mark)
{
	if (!payload || !payload_len)
		return NULL;

	DWORD desc = VP8DescriptorLen(payload, payload_len);
	if (!desc || desc == payload_len)
	{
		// Descripteur incohérent ou paquet sans données VP8 : la trame en
		// cours est irrécupérable (un trou au milieu d'un échantillon vidéo
		// le rend indécodable).
		ResetFrame();
		return NULL;
	}

	bool S    = (payload[0] >> 4) & 0x01;
	BYTE pid  = payload[0] & 0x07;

	// Début de trame : S=1 sur la première partition
	if (S && pid == 0)
	{
		// Une trame précédente jamais terminée (mark perdu) traîne encore :
		// on la jette plutôt que de coller deux trames dans un échantillon.
		if (frame.GetLength())
			ResetFrame();
		started = true;

		const BYTE* p = payload + desc;
		DWORD left = payload_len - desc;
		// Payload header VP8 : frame tag de 3 octets little-endian, dont le
		// bit 0 du premier octet est frame_type (0 = trame clé).
		if (!(p[0] & 0x01))
		{
			frame.SetIntra(true);
			// Keyframe header non compressé : start code 9d 01 2a puis
			// largeur et hauteur sur 14 bits little-endian (2 bits d'échelle
			// ignorés). C'est lui qui donne ses dimensions à la piste MP4.
			if (left >= 10 && p[3] == 0x9d && p[4] == 0x01 && p[5] == 0x2a)
			{
				frame.SetWidth(((p[7] << 8) | p[6]) & 0x3fff);
				frame.SetHeight(((p[9] << 8) | p[8]) & 0x3fff);
			}
		}
	}
	else if (!started)
	{
		// Milieu de trame sans en avoir vu le début (arrivée en cours de
		// flux, perte du paquet de tête) : rien d'exploitable.
		return NULL;
	}

	// Borne de sûreté (cf. MaxFrameSize dans l'en-tête)
	if (frame.GetLength() + payload_len > MaxFrameSize)
	{
		Log("-VP8Depacketizer: trame de plus de %u octets, abandonnée\n", MaxFrameSize);
		ResetFrame();
		return NULL;
	}

	frame.AppendMedia(payload + desc, payload_len - desc);

	return &frame;
}
