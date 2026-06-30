#include <string.h>
#include <netinet/in.h>
#include "medkit/log.h"
#include "h263codec.h"
#include "medkit/video.h"
#include "../ffvideocodec.h"

//////////////////////////////////////////////////////////////////////////
//Encoder
// 	Codificador H263
//
//////////////////////////////////////////////////////////////////////////
/***********************
* H263Encoder
*	Constructor de la clase
************************/
H263Encoder::H263Encoder(const Properties& properties):
	FfVideoEncoder(properties, AV_CODEC_ID_H263P, VideoCodec::H263_1998)
{
}

/***********************
* ~H263Encoder
*	Destructor
************************/
H263Encoder::~H263Encoder()
{
}

//////////////////////////////////////////////////////////////////////////
//H263Decoder
// 	Decodificador H263
//
//////////////////////////////////////////////////////////////////////////
/***********************
* H263Decoder
*	Consturctor
************************/
H263Decoder::H263Decoder():
	FfVideoDecoder(AV_CODEC_ID_H263P, VideoCodec::H263_1998)
{
}

/***********************
* ~H263Decoder
*	Destructor
************************/
H263Decoder::~H263Decoder()
{
}

/***********************
* H263Decoder::DecodePacket
*	Retire l'en-tête de payload RFC 2429 (H.263+) puis accumule ; décode sur 'last'.
************************/
int H263Decoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	// place disponible (+ padding ffmpeg + 2 octets de start code éventuels)
	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE+2 > bufSize)
	{
		Log("-H263 DecodePacket buffer size error, reseting\n");
		bufLen = 0;
		return 0;
	}

	if (inLen)
	{
		/*    0                   1
		      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
		     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		     |   RR    |P|V|   PLEN    |PEBIT|
		     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
		BYTE p    = in[0] & 0x04;
		BYTE v    = in[0] & 0x02;
		BYTE plen = ((in[0] & 0x1) << 5) | (in[1] >> 3);

		/* Saute l'en-tête + l'en-tête d'image supplémentaire */
		BYTE* i  = in+2+plen;
		DWORD len = inLen-2-plen;

		/* Octet VRC supplémentaire */
		if (v)
		{
			i++;
			len--;
		}

		/* Bit P : premier paquet de trame -> préfixer 2 octets 0 (start code) */
		if (p)
		{
			buffer[bufLen]   = 0;
			buffer[bufLen+1] = 0;
			bufLen += 2;
		}

		memcpy(buffer+bufLen,i,len);
		bufLen += len;
	}

	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		ret = Decode(buffer,bufLen);
		bufLen = 0;
	}
	return ret;
}
