/*
 * File:   vp8decoder.cpp
 *
 * Décodeur VP8 natif ffmpeg + dépaquetisation RTP VP8 (RFC 7741).
 */
#include <string.h>
#include <sstream>
#include "medkit/log.h"
#include "vp8decoder.h"

// Fonction pour construire l'entête fmtp pour VP8 avec payloadType en paramètre
bool buildVP8FmtpHeader(AVCodecContext* ctx, int payloadType, std::string &fmtp2)
{
    std::ostringstream fmtp;

    // Ajouter le payload type
    fmtp << "a=fmtp:" << payloadType;

    // Paramètres optionnels pour VP8
    // Exemple : max-fr (taux d'images maximal) et max-fs (taille maximale des trames)
    // Ces valeurs peuvent être extraites de ctx si nécessaire
    int maxFrameRate = ctx->framerate.num > 0 && ctx->framerate.den > 0 ?
                       ctx->framerate.num / ctx->framerate.den : 30;
    int maxFrameSize = 4000; // Valeur par défaut, à adapter selon votre cas

    // Ajouter max-fr si disponible
    if (maxFrameRate > 0) {
        fmtp << " max-fr=" << maxFrameRate;
    }

    // Ajouter max-fs si nécessaire
    if (maxFrameSize > 0) 
	{
		if (!fmtp.str().empty()) fmtp << ";";
		fmtp << " max-fs=" << maxFrameSize;
    }

    fmtp2 = fmtp.str();
    return fmtp2.length() > 0;
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
