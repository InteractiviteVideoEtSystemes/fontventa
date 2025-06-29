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
