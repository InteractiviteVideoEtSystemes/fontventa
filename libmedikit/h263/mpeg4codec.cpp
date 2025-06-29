#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "medkit/log.h"
#include "mpeg4codec.h"
#include "medkit/video.h"

//////////////////////////////////////////////////////////////////////////
//Mpeg4Decoder
// 	Decodificador MPEG4
//
//////////////////////////////////////////////////////////////////////////

/***********************
* Mpeg4Decoder
*	Consturctor
************************/
Mpeg4Decoder::Mpeg4Decoder():
	FfVideoDecoder(AV_CODEC_ID_MPEG4, VideoCodec::MPEG4)
{
}

/***********************
* ~Mpeg4Decoder
*	Destructor
************************/
Mpeg4Decoder::~Mpeg4Decoder()
{
}


/******************************************************
* Mpeg4Encoder
*
*******************************************************/
/***********************
* Mpeg4Encoder
*	Constructor de la clase
************************/
Mpeg4Encoder::Mpeg4Encoder(const Properties& properties):
	FfVideoEncoder(properties, AV_CODEC_ID_MPEG4, VideoCodec::MPEG4)
{
}

/***********************
* ~Mpeg4Encoder
*	Destructor
************************/
Mpeg4Encoder::~Mpeg4Encoder()
{
}
