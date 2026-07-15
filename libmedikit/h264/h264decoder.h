#ifndef _H264DECODER_H_
#define _H264DECODER_H_

#include "../ffvideocodec.h"

class H264Decoder : public FfVideoDecoder
{
public:
	H264Decoder();
	virtual ~H264Decoder();

	// Dépaquetisation RTP H.264 (RFC 6184 : single NAL, STAP-A, FU-A).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);
	// Pas de GetFmtpInfo ici : un décodeur n'origine jamais de SPS/PPS (il ne
	// fait que réassembler le flux Annex-B reçu) ; le fmtp se construit côté
	// H264Encoder, à partir des NAL qu'il produit lui-même (cf. h264encoder.h).
};
#endif
