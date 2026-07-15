#ifndef _H263CODEC_H_
#define _H263CODEC_H_

#include "../medkit/h263packet.h"
#include "../ffvideocodec.h"
#include <list>

class H263Encoder : public FfVideoEncoder
{
public:
	H263Encoder(const Properties& properties);
	virtual ~H263Encoder();
};

class H263Decoder : public FfVideoDecoder
{
public:
	H263Decoder();
	virtual ~H263Decoder();

	// Dépaquetisation RTP H.263+ (RFC 2429 : retrait de l'en-tête de payload).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last);

	// H.263+ décodeur ffmpeg (AV_CODEC_ID_H263P), cf. h263codec.cpp.
	static bool IsSupported() { return FfVideoDecoder::IsCodecAvailable(AV_CODEC_ID_H263P); }
};

#endif
