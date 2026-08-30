/*
 * File:   vp8frameheader.h
 *
 * Lecture de l'en-tête de trame VP8 (RFC 6386 §9.1-9.7) jusqu'aux drapeaux de
 * mise à jour des références long-terme (golden/altref). C'est ce qui permet
 * d'acquitter une trame de référence par RPSI (RFC 7741 §5.1) : notre décodage
 * passe par ffmpeg, qui ne publie pas l'équivalent du
 * VP8D_GET_LAST_REF_UPDATES de libvpx.
 *
 * Octets purs, sans ffmpeg : le décodeur booléen minimal (§7.3) ne parcourt
 * que le début du premier header compressé, jamais les macroblocs.
 */
#ifndef VP8FRAMEHEADER_H
#define VP8FRAMEHEADER_H

#include "../medkit/config.h"

struct VP8FrameHeaderInfo
{
	bool keyFrame      = false;
	bool showFrame     = false;
	// Trame clé : golden et altref sont remplacées d'office (§9.7), les
	// drapeaux sont posés sans lecture de bits.
	bool refreshGolden = false;
	bool refreshAltRef = false;
	BYTE copyToGolden  = 0;	// 0=aucune, 1=last, 2=altref (§9.7)
	BYTE copyToAltRef  = 0;	// 0=aucune, 1=last, 2=golden

	// La trame met à jour golden ou altref, par remplacement ou par copie :
	// c'est la condition d'acquittement RPSI, le même périmètre que ce que
	// VP8D_GET_LAST_REF_UPDATES rend au décodeur mediastreamer2.
	bool UpdatesReference() const
	{
		return refreshGolden || refreshAltRef || copyToGolden || copyToAltRef;
	}
};

// Trame VP8 complète (payload descriptor RTP déjà retiré). false si l'en-tête
// est tronqué ou incohérent — ne jamais acquitter dans ce cas.
bool VP8ParseFrameHeader(const BYTE* data, DWORD size, VP8FrameHeaderInfo &info);

#endif	/* VP8FRAMEHEADER_H */
