/*
 * File:   vp8decoder.cpp
 *
 * Décodeur VP8 natif ffmpeg + dépaquetisation RTP VP8 (RFC 7741).
 */
#include <string.h>
#include <sstream>
#include "medkit/log.h"
#include "vp8decoder.h"
#include "vp8depacketizer.h"
#include "vp8frameheader.h"

// Construit "a=fmtp:<pt> max-fr=X;max-fs=Y" (RFC 7742) à partir de limites déjà
// résolues par l'appelant (encodeur : ses propres bornes configurées ;
// décodeur : sa capacité de décodage locale). Un booléen explicite marque
// qu'un paramètre a déjà été ajouté, plutôt que de tester si le flux
// accumulé est vide (celui-ci contient toujours "a=fmtp:<pt>", donc le test
// était systématiquement vrai — bug corrigé ici).
// Paramètres VP8 seuls "max-fr=X;max-fs=Y" (RFC 7742), SANS "a=fmtp:<pt> ", à
// partir de limites déjà résolues. Chaîne vide si aucune limite active.
std::string BuildVP8FmtpParams(int maxFrameRate, int maxFrameSize)
{
    std::ostringstream fmtp;
    bool any = false;

    if (maxFrameRate > 0)
    {
        fmtp << "max-fr=" << maxFrameRate;
        any = true;
    }

    if (maxFrameSize > 0)
    {
        fmtp << (any ? ";" : "") << "max-fs=" << maxFrameSize;
        any = true;
    }

    return fmtp.str();
}

// Forme « ligne complète » historique : "a=fmtp:<pt> <params>".
bool BuildVP8FmtpFromLimits(int payloadType, int maxFrameRate, int maxFrameSize, std::string &fmtp2)
{
    std::string params = BuildVP8FmtpParams(maxFrameRate, maxFrameSize);

    std::ostringstream fmtp;
    fmtp << "a=fmtp:" << payloadType;
    if (!params.empty())
        fmtp << " " << params;

    fmtp2 = fmtp.str();
    return !params.empty();
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

/* Le parseur du payload descriptor (RFC 7741) vit dans vp8depacketizer.cpp
 * (VP8DescriptorLen) : une seule implémentation, partagée avec le
 * dépaquetiseur — une copie locale dériverait. */

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
		DWORD pos = VP8DescriptorLen(in, inLen);
		if (!pos)
		{
			Log("-VP8 payload descriptor invalide, reset\n");
			bufLen = 0;
			return 0;
		}
		// PictureID lu sur le paquet de tête de trame (RFC 7741 : la valeur
		// est identique sur tous les paquets d'une même trame)
		if (!bufLen)
			hasPictureId = VP8DescriptorPictureId(in, inLen, pictureId);
		memcpy(buffer+bufLen, in+pos, inLen-pos);
		bufLen += inLen-pos;
	}

	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		// Drapeaux de mise à jour des références, lus AVANT le décodage : le
		// tampon est vidé ensuite. L'acquittement RPSI n'est armé que si la
		// trame se décode (RFC 4585 §6.3.3 : le RPSI désigne une référence
		// que le récepteur POSSÈDE).
		VP8FrameHeaderInfo header;
		bool parsed = VP8ParseFrameHeader(buffer, bufLen, header);
		ret = Decode(buffer,bufLen);
		ackReady = ret && parsed && hasPictureId && header.UpdatesReference();
		if (ackReady)
			ackPictureId = pictureId;
		hasPictureId = false;
		bufLen = 0;
	}
	return ret;
}

bool VP8Decoder::GetReferencePictureId(WORD &pid)
{
	if (!ackReady)
		return false;
	pid = ackPictureId;
	return true;
}

bool VP8Decoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	return buildVP8FmtpHeader(ctx, payloadType, fmtp);
}
