#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <vector>
#include <netinet/in.h>
#include "../medkit/log.h"
#include "h264decoder.h"

// Fonction pour extraire SPS et PPS depuis extradata
static bool extractSPSAndPPS(const uint8_t* extradata, size_t extradataSize,
                      std::vector<uint8_t>& sps, std::vector<uint8_t>& pps)
{
    if (extradataSize < 8) 
	{
		Log("H264: Extradata too small to contain SPS and PPS\n");
        return false;
    }

    // Chercher le début du SPS (après 0x00 0x00 0x00 0x01)
    for (size_t i = 0; i < extradataSize - 4; ++i) 
	{
        if (extradata[i] == 0 && extradata[i+1] == 0 &&
            extradata[i+2] == 0 && extradata[i+3] == 1)
		{
            // Le SPS commence à i+4
            size_t spsStart = i + 4;
            // La taille du SPS est encodée sur 2 octets (big-endian)
            size_t spsSize = (extradata[spsStart - 2] << 8) | extradata[spsStart - 1];
            if (spsStart + spsSize > extradataSize) {
                return false;
            }
            sps.assign(extradata + spsStart, extradata + spsStart + spsSize);

            // Chercher le PPS (après le SPS)
            size_t ppsStart = spsStart + spsSize;
            while (ppsStart < extradataSize - 4) {
                if (extradata[ppsStart] == 0 && extradata[ppsStart+1] == 0 &&
                    extradata[ppsStart+2] == 0 && extradata[ppsStart+3] == 1) {
                    ppsStart += 4;
                    size_t ppsSize = (extradata[ppsStart - 2] << 8) | extradata[ppsStart - 1];
                    if (ppsStart + ppsSize > extradataSize) {
                        return false;
                    }
                    pps.assign(extradata + ppsStart, extradata + ppsStart + ppsSize);
                    return true;
                }
                ++ppsStart;
            }
        }
    }
    return false;
}

#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <cstdint>

extern "C" {
#include <libavutil/base64.h>
}

std::string Base64Encode(const std::vector<uint8_t> &binary)
{
    int sz = AV_BASE64_SIZE(binary.size()); // macro officielle FFmpeg
    std::vector<char> buf(sz);

    char *result = av_base64_encode(buf.data(), sz, binary.data(),
                                     static_cast<int>(binary.size()));
    if (!result) 
	{
		Error("H264: buffer to small to convert to base64"); 
		return "";
	}
    
    return std::string(result);
}

std::string BuildH264Fmtp(int payloadType, AVCodecContext *ctx)
{
    if (!ctx)
        return "";

    std::vector<uint8_t> sps, pps;

    if (!extractSPSAndPPS(ctx->extradata, ctx->extradata_size, sps, pps))
    {
        Error("H264: Failed to extract SPS and PPS from extradata\n");
        return "";
    }

    if (sps.size() < 4)
    {
        Error("H264: Invalid SPS size\n");
        return "";
    }

    // profile_idc, constraint flags, level_idc — nécessite que sps[0] soit le header NAL
    uint8_t profileIdc      = sps[1];
    uint8_t constraintFlags = sps[2];
    uint8_t levelIdc        = sps[3];

    std::string spsB64 = Base64Encode(sps);
    std::string ppsB64 = Base64Encode(pps);

    std::ostringstream fmtp;
    fmtp << "a=fmtp:" << payloadType << " profile-level-id="
         << std::hex << std::setfill('0')
         << std::setw(2) << static_cast<int>(profileIdc)
         << std::setw(2) << static_cast<int>(constraintFlags)
         << std::setw(2) << static_cast<int>(levelIdc)
         << "; sprop-parameter-sets=" << spsB64 << "," << ppsB64
         << "; packetization-mode=1";

    return fmtp.str();
}

//H264Decoder
// 	Decodificador H264
//
//////////////////////////////////////////////////////////////////////////
/***********************
* H264Decoder
*	Consturctor
************************/
H264Decoder::H264Decoder():
	FfVideoDecoder(AV_CODEC_ID_H264, VideoCodec::H264)
{
}

/***********************
* ~H264Decoder
*	Destructor
************************/
H264Decoder::~H264Decoder()
{
}
/* 3 zero bytes syncword */
static const uint8_t sync_bytes[] = { 0, 0, 0, 1 };

/* TODO:
	- Delete unused method/function...
*/

/***********************
* h264_append_nals
*	Dépaquetise un payload RTP H.264 (RFC 6184 : single NAL, STAP-A, FU-A) et
*	l'écrit en flux Annex-B (préfixé start-code) dans dest+destLen. Retourne le
*	nombre d'octets ajoutés. Porté depuis mcu/src/h264/h264decoder.cpp.
************************/
static DWORD h264_append_nals(BYTE *dest, DWORD destLen, DWORD destSize, BYTE *buffer, DWORD bufferLen)
{
	BYTE nal_unit_type;
	unsigned int nalu_size;

	DWORD payload_len = bufferLen;
	BYTE *payload = buffer;
	BYTE *outdata = dest+destLen;
	DWORD outsize = 0;

	if (!bufferLen)
		return 0;

	/* +---------------+
	 * |F|NRI|  Type   |
	 * +---------------+ */
	nal_unit_type = payload[0] & 0x1f;

	switch (nal_unit_type)
	{
		case 0:
		case 30:
		case 31:
			return 0;	/* undefined */
		case 25:
			return 0;	/* STAP-B : non supporté */
		case 24:
		{
			/* STAP-A : single-time aggregation packet (5.7.1) */
			payload++;
			payload_len--;

			while (payload_len > 2)
			{
				nalu_size = (payload[0] << 8) | payload[1];
				payload += 2;
				payload_len -= 2;

				if (nalu_size > payload_len)
					nalu_size = payload_len;

				outsize += nalu_size + sizeof (sync_bytes);
				if (outsize + destLen > destSize)
					return Error("Frame to small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata += nalu_size;

				payload += nalu_size;
				payload_len -= nalu_size;
			}
			return outsize;
		}
		case 26:
		case 27:
			return 0;	/* MTAP16/MTAP24 : non supporté */
		case 28:
		case 29:
		{
			/* FU-A / FU-B : Fragmentation unit (5.8) */
			BYTE S = (payload[1] & 0x80) == 0x80;

			if (S)
			{
				BYTE nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);
				payload += 1;
				payload_len -= 1;

				nalu_size = payload_len;
				outsize = nalu_size + sizeof (sync_bytes);
				if (outsize + destLen > destSize)
					return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata[0] = nal_header;
				outdata += nalu_size;
				return outsize;
			}
			else
			{
				payload += 2;
				payload_len -= 2;

				outsize = payload_len;
				if (outsize + destLen > destSize)
					return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
				memcpy (outdata, payload, outsize);
				return outsize;
			}
		}
		default:
		{
			/* 1-23 : Single NAL unit packet (5.6) */
			nalu_size = payload_len;
			outsize = nalu_size + sizeof (sync_bytes);
			if (outsize + destLen > destSize)
				return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
			memcpy (outdata, sync_bytes, sizeof (sync_bytes));
			outdata += sizeof (sync_bytes);
			memcpy (outdata, payload, nalu_size);
			outdata += nalu_size;
			return outsize;
		}
	}
	return 0;
}

/***********************
* H264Decoder::DecodePacket
*	Reconstruit le flux Annex-B (start codes) depuis les NAL RTP, puis décode
*	la trame complète sur 'last'.
************************/
int H264Decoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE > bufSize)
	{
		Log("-H264 DecodePacket buffer size error, reseting\n");
		bufLen = 0;
		return 0;
	}

	bufLen += h264_append_nals(buffer,bufLen,bufSize-AV_INPUT_BUFFER_PADDING_SIZE,in,inLen);

	if (last)
	{
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		ret = Decode(buffer,bufLen);
		bufLen = 0;
	}
	return ret;
}

bool H264Decoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	if (!ctx) return false;

	fmtp = BuildH264Fmtp(payloadType, ctx);
	return !fmtp.empty();
}

#if 0
DWORD h264_append_nals(BYTE *dest, DWORD destLen, DWORD destSize, BYTE *buffer, DWORD bufferLen, BYTE **nals, DWORD nalSize, DWORD *num)
{
	BYTE nal_unit_type;
	unsigned int header_len;
	BYTE nal_ref_idc;
	unsigned int nalu_size;

	DWORD payload_len = bufferLen;
	BYTE *payload = buffer;
	BYTE *outdata = dest+destLen;
	DWORD outsize = 0;

	//No nals
	*num = 0;

	//Check
	if (!bufferLen)
		//Exit
		return 0;


	/* +---------------+
	 * |0|1|2|3|4|5|6|7|
	 * +-+-+-+-+-+-+-+-+
	 * |F|NRI|  Type   |
	 * +---------------+
	 *
	 * F must be 0.
	 */
	nal_ref_idc = (payload[0] & 0x60) >> 5;
	nal_unit_type = payload[0] & 0x1f;
	//printf("[NAL:%x,type:%x]\n", payload[0], nal_unit_type);

	/* at least one byte header with type */
	header_len = 1;

	switch (nal_unit_type)
	{
		case 0:
		case 30:
		case 31:
			/* undefined */
			return 0;
		case 25:
			/* STAP-B		Single-time aggregation packet		 5.7.1 */
			/* 2 byte extra header for DON */
			/** Not supported */
			return 0;
		case 24:
		{
			/**
			   Figure 7 presents an example of an RTP packet that contains an STAP-
			   A.  The STAP contains two single-time aggregation units, labeled as 1
			   and 2 in the figure.

			       0                   1                   2                   3
			       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                          RTP Header                           |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |STAP-A NAL HDR |         NALU 1 Size           | NALU 1 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 1 Data                           |
			      :                                                               :
			      +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |               | NALU 2 Size                   | NALU 2 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 2 Data                           |
			      :                                                               :
			      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                               :...OPTIONAL RTP padding        |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

			      Figure 7.  An example of an RTP packet including an STAP-A and two
					 single-time aggregation units
			*/

			/* Skip STAP-A NAL HDR */
			payload++;
			payload_len--;

			/* STAP-A Single-time aggregation packet 5.7.1 */
			while (payload_len > 2)
			{
				/* Get NALU size */
				nalu_size = (payload[0] << 8) | payload[1];

				/* strip NALU size */
				payload += 2;
				payload_len -= 2;

				if (nalu_size > payload_len)
					nalu_size = payload_len;

				outsize += nalu_size + sizeof (sync_bytes);

				/* Check size */
				if (outsize + destLen >destSize)
					return Error("Frame to small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);

				//Set nal
				if (nals && nalSize-1>*num)
					//Add it
					nals[(*num)++] = outdata;

				memcpy (outdata, payload, nalu_size);
				outdata += nalu_size;

				payload += nalu_size;
				payload_len -= nalu_size;
			}

			return outsize;
		}
		case 26:
			/* MTAP16 Multi-time aggregation packet	5.7.2 */
			header_len = 5;
			return 0;
			break;
		case 27:
			/* MTAP24 Multi-time aggregation packet	5.7.2 */
			header_len = 6;
			return 0;
			break;
		case 28:
		case 29:
		{
			/* FU-A	Fragmentation unit	 5.8 */
			/* FU-B	Fragmentation unit	 5.8 */
			BYTE S, E;

			/* +---------------+
			 * |0|1|2|3|4|5|6|7|
			 * +-+-+-+-+-+-+-+-+
			 * |S|E|R| Type	   |
			 * +---------------+
			 *
			 * R is reserved and always 0
			 */
			S = (payload[1] & 0x80) == 0x80;
			E = (payload[1] & 0x40) == 0x40;

			if (S)
			{
				/* NAL unit starts here */
				BYTE nal_header;

				/* reconstruct NAL header */
				nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);

				/* strip type header, keep FU header, we'll reuse it to reconstruct
				 * the NAL header. */
				payload += 1;
				payload_len -= 1;

				nalu_size = payload_len;
				outsize = nalu_size + sizeof (sync_bytes);

				//Check size
				if (outsize + destLen >destSize)
					return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);

				//Set nal
				if (nals && nalSize-1>*num)
					//Add it
					nals[(*num)++] = outdata;

				memcpy (outdata, payload, nalu_size);
				outdata[0] = nal_header;
				outdata += nalu_size;
				return outsize;

			} else {
				/* strip off FU indicator and FU header bytes */
				payload += 2;
				payload_len -= 2;

				outsize = payload_len;
				//Check size
				if (outsize + destLen >destSize)
					return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
				memcpy (outdata, payload, outsize);
				outdata += nalu_size;
				return outsize;
			}

			break;
		}
		default:
		{
			/* 1-23	 NAL unit	Single NAL unit packet per H.264	 5.6 */
			/* the entire payload is the output buffer */
			nalu_size = payload_len;
			outsize = nalu_size + sizeof (sync_bytes);
			//Check size
			if (outsize + destLen >destSize)
				return Error("Frame too small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
			memcpy (outdata, sync_bytes, sizeof (sync_bytes));
			outdata += sizeof (sync_bytes);

			//Set nal
			if (nals && nalSize-1>*num)
				//Add it
				nals[(*num)++] = outdata;

			memcpy (outdata, payload, nalu_size);
			outdata += nalu_size;

			return outsize;
		}
	}

	return 0;
}

DWORD h264_append(BYTE *dest, DWORD destLen, DWORD destSize, BYTE *buffer, DWORD bufferLen)
{
	DWORD num = 0;
	return h264_append_nals(dest,destLen,destSize,buffer,bufferLen,NULL,0,&num);
}
#endif

/***********************
* DecodePacket
*	Decodifica un packete
************************/
/*int H264Decoder::DecodePacket(BYTE *in,DWORD inLen,int lost,int last)
{
	int ret = 1;

	// Check total length
	if (bufLen+inLen+AV_INPUT_BUFFER_PADDING_SIZE>bufSize)
	{
		// Reset buffer
		bufLen = 0;

		// Exit
		return 0;
	}

	//Aumentamos la longitud
	bufLen += h264_append(buffer,bufLen,bufSize-AV_INPUT_BUFFER_PADDING_SIZE,in,inLen);

	//Si es el ultimo
	if(last)
	{
		//Borramos el final
		memset(buffer+bufLen,0,AV_INPUT_BUFFER_PADDING_SIZE);
		//Decode
		ret = Decode(buffer,bufLen);
		//Y resetamos el buffer
		bufLen=0;
	}
	//Return
	return ret;
}*/

/*int H264Decoder::Decode(BYTE *buffer,DWORD size)
{
	//Decodificamos
	int got_picture=0;
	//Decodificamos
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = buffer;
	pkt.size = size;
	int readed = avcodec_decode_video2(ctx, picture, &got_picture, &pkt);

	//Si hay picture
	if (got_picture && readed>0)
	{
		if(ctx->width==0 || ctx->height==0)
			return Error("-Wrong dimmensions [%d,%d]\n",ctx->width,ctx->height);

		int w = ctx->width;
		int h = ctx->height;
		int u = w*h;
		int v = w*h*5/4;
		int size = w*h*3/2;

		//Comprobamos el tama�o
		if (size>frameSize)
		{
			Log("-Frame size %dx%d\n",w,h);
			//Liberamos si habia
			if(frame!=NULL)
				free(frame);
			//Y allocamos de nuevo
			frame = (BYTE*) malloc(size);
			frameSize = size;
		}


		//Copaamos  el Cy
		for(int i=0;i<ctx->height;i++)
			memcpy(&frame[i*w],&picture->data[0][i*picture->linesize[0]],w);

		//Y el Cr y Cb
		for(int i=0;i<ctx->height/2;i++)
		{
			memcpy(&frame[i*w/2+u],&picture->data[1][i*picture->linesize[1]],w/2);
			memcpy(&frame[i*w/2+v],&picture->data[2][i*picture->linesize[2]],w/2);
		}
		return 2;
	}
	return 1;
}*/

