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
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType);
};
#endif
