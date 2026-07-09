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
AV1Encoder::AV1Encoder():
	FfVideoEncoder(AV_CODEC_ID_AV1, VideoCodec::AV1)
{
}

/***********************
* ~AV1Encoder
*	Destructor
************************/
AV1Encoder::~AV1Encoder()
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
AV1Encoder::AV1Encoder(const Properties& properties):
	FfVideoEncoder(properties, AV_CODEC_ID_AV1, VideoCodec::AV1)
{
}

/***********************
* ~AV1Encoder
*	Destructor
************************/
AV1Encoder::~AV1Encoder()
{
}
