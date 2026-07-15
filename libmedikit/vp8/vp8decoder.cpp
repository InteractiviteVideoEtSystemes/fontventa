/*
 * File:   vp8decoder.cpp
 *
 * Décodeur VP8 natif ffmpeg + dépaquetisation RTP VP8 (RFC 7741).
 */
#include <string.h>
#include <sstream>
#include "medkit/log.h"
#include "vp8decoder.h"

// Construit "a=fmtp:<pt> max-fr=X;max-fs=Y" (RFC 7742) à partir de limites déjà
// résolues par l'appelant (encodeur : ses propres bornes configurées ;
// décodeur : sa capacité de décodage locale). Un booléen explicite marque
// qu'un paramètre a déjà été ajouté, plutôt que de tester si le flux
// accumulé est vide (celui-ci contient toujours "a=fmtp:<pt>", donc le test
// était systématiquement vrai — bug corrigé ici).
bool BuildVP8FmtpFromLimits(int payloadType, int maxFrameRate, int maxFrameSize, std::string &fmtp2)
{
    std::ostringstream fmtp;
    fmtp << "a=fmtp:" << payloadType;

    bool any = false;

    if (maxFrameRate > 0)
    {
        fmtp << " max-fr=" << maxFrameRate;
        any = true;
    }

    if (maxFrameSize > 0)
    {
        fmtp << (any ? ";" : " ") << "max-fs=" << maxFrameSize;
        any = true;
    }

    fmtp2 = fmtp.str();
    return any;
}

// Rapporte la capacité de décodage LOCALE (ce que ce décodeur peut encaisser
// en réception, au sens RFC 7742 §6.2 — pas les limites de l'encodeur
// distant). ctx->framerate ne reflète jamais une vraie contrainte ici (rien
// dans FfVideoDecoder ne l'alimente) : valeurs fixes documentées, cohérentes
// avec le tampon de décodage réel (cf. FfVideoDecoder, bufSize=1024x756).
bool buildVP8FmtpHeader(AVCodecContext* ctx, int payloadType, std::string &fmtp2)
{
    const int localMaxFrameRate = 30;
    const int localMaxFrameSize = 3072; // macroblocs 16x16 pour ~1024x756

    return BuildVP8FmtpFromLimits(payloadType, localMaxFrameRate, localMaxFrameSize, fmtp2);
}


VP8Decoder::VP8Decoder() :
	FfVideoDecoder(AV_CODEC_ID_VP8, VideoCodec::VP8)
{
}

VP8Decoder::~VP8Decoder()
{
}

/***********************
* vp8_descriptor_len
*	Longueur (octets) du VP8 payload descriptor (RFC 7741) à retirer en tête de
*	payload RTP. Porté depuis mcu/src/vp8/vp8.h (VP8PayloadDescriptor::Parse).
************************/
static DWORD vp8_descriptor_len(BYTE* data, DWORD size)
{
	if (size < 1)
		return 0;

	DWORD len = 1;
	/*  0 1 2 3 4 5 6 7
	 * +-+-+-+-+-+-+-+-+
	 * |X|R|N|S|PartID |
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

/***********************
* VP8Decoder::DecodePacket
*	Retire le payload descriptor VP8, accumule la partition, décode sur 'last'.
************************/
int VP8Decoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE > bufSize)
	{
		Log("-VP8 DecodePacket buffer size error, reseting\n");
		bufLen = 0;
		return 0;
	}

	if (inLen)
	{
		DWORD pos = vp8_descriptor_len(in, inLen);
		if (!pos)
		{
			Log("-VP8 payload descriptor invalide, reset\n");
			bufLen = 0;
			return 0;
		}
		memcpy(buffer+bufLen, in+pos, inLen-pos);
		bufLen += inLen-pos;
	}

	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		ret = Decode(buffer,bufLen);
		bufLen = 0;
	}
	return ret;
}

bool VP8Decoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	return buildVP8FmtpHeader(ctx, payloadType, fmtp);
}
