/*
 * File:   vp8decoder.h
 *
 * Décodeur VP8 adossé au décodeur NATIF ffmpeg (AV_CODEC_ID_VP8, pas libvpx :
 * avcodec_find_decoder renvoie le décodeur natif `vp8` par défaut). Porte sa
 * propre dépaquetisation RTP (VP8 payload descriptor, RFC 7741).
 */
#ifndef _VP8DECODER_H_
#define _VP8DECODER_H_

#include "../ffvideocodec.h"

class VP8Decoder : public FfVideoDecoder
{
public:
	VP8Decoder();
	virtual ~VP8Decoder();

	// Dépaquetisation RTP VP8 (RFC 7741 : retrait du payload descriptor).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);
};

#endif
