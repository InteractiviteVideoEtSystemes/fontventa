#include "medkit/log.h"
#include "medkit/video.h"
#include "h263/h263codec.h"
#include "h264/h264encoder.h"
#include "h264/h264decoder.h"
#include "vp8/vp8decoder.h"
#include "vp8/vp8encoder.h"
#include "ffvideocodec.h"


bool VideoFrame::Packetize(unsigned int mtu)
{
	switch(codec)
	{
		case VideoCodec::H263_1998:
			return PacketizeH263(mtu);
		case VideoCodec::H264:
			return PacketizeH264(mtu);
		default:
			Error("Dont know how to packetize video frame for codec [%d]\n",codec);
	}
	return false;
}

VideoDecoder* VideoCodecFactory::CreateDecoder(VideoCodec::Type codec)
{
	Log("-CreateVideoDecoder[%d,%s]\n",codec,VideoCodec::GetNameFor(codec));

	switch(codec)
	{
		case VideoCodec::SORENSON:
			return new FfVideoDecoder(AV_CODEC_ID_FLV1, VideoCodec::SORENSON);
		case VideoCodec::H263_1998:
			return new H263Decoder();
		case VideoCodec::H263_1996:
			return new FfVideoDecoder(AV_CODEC_ID_H263, VideoCodec::H263_1996);
		case VideoCodec::MPEG4:
			return new FfVideoDecoder(AV_CODEC_ID_MPEG4, VideoCodec::MPEG4);
		case VideoCodec::H264:
			return new H264Decoder();
		case VideoCodec::VP6:
			return new FfVideoDecoder(AV_CODEC_ID_VP6F, VideoCodec::VP6);
		case VideoCodec::VP8:
			return new VP8Decoder();
		default:
			Error("Video decoder not found [%d]\n",codec);
	}
	return NULL;
}

VideoEncoder* VideoCodecFactory::CreateEncoder(VideoCodec::Type codec)
{
	Properties properties;
	return CreateEncoder(codec, properties);
}

VideoEncoder* VideoCodecFactory::CreateEncoder(VideoCodec::Type codec, const Properties& properties)
{
	Log("-CreateVideoEncoder[%d,%s]\n",codec,VideoCodec::GetNameFor(codec));

	switch(codec)
	{
		case VideoCodec::SORENSON:
			return new FfVideoEncoder(properties, AV_CODEC_ID_FLV1, VideoCodec::SORENSON);
		case VideoCodec::H263_1998:
			return new FfVideoEncoder(properties, AV_CODEC_ID_H263P, VideoCodec::H263_1998);
		case VideoCodec::H263_1996:
			return new FfVideoEncoder(properties, AV_CODEC_ID_H263, VideoCodec::H263_1996);
		case VideoCodec::MPEG4:
			return new FfVideoEncoder(properties, AV_CODEC_ID_MPEG4, VideoCodec::MPEG4);
		case VideoCodec::H264:
			return new H264Encoder(properties);
		case VideoCodec::VP8:
			return new VP8Encoder(properties);
		default:
			Error("Video Encoder not found\n");
	}
	return NULL;
}


DWORD VideoFrame::ReadNaluSize(BYTE * data)
{
	switch(naluSizeLen)
	{
		case 0:
			return 0;
		case 1:
			return data[0];
		case 2:
			return (data[0] << 8) | data[1];
		case 3:
			return (data[0] << 16) |(data[1] << 8) | data[2];
		default:
			return (data[0] << 24) |(data[1] << 16) |(data[2] << 8) | data[3];
	}
}

DWORD VideoFrame::DetectNaluBoundary(BYTE * p, DWORD sz)
{
	DWORD l;

	for (l = 0; l+4 < sz; l++)
	{
		if (p[l] == 0 && p[l+1] == 0)
		{
			if (p[l+2] == 1)
				return l;
		}
		else if(p[l+2] == 0 && p[l+3] == 1)
		{
			return l;
		}
	}

	if (l+3 < sz)
	{
		if (p[l] == 0 && p[l+1] == 0 && p[l+2] == 1)
			return l;
	}

	return 0;
}

#define H264_FUA_HEADER_SIZE 2

bool VideoFrame::PacketizeH264(unsigned int mtu)
{
	BYTE * p = GetData();
	unsigned int l = 0;
	DWORD naluSz;

	ClearRTPPacketizationInfo();

	if (p[l] == 0 && p[l+1] == 0)
	{
		if (p[l+2] == 1)
			l += 3;
	}
	else if(p[l+2] == 0 && p[l+3] == 1)
	{
		l += 4;
	}

	while (l < GetLength())
	{
		if (useStartCode)
			naluSz = DetectNaluBoundary(p + l, GetLength() - l);
		else
			naluSz = ReadNaluSize(p + l);

		if (naluSz == 0 || naluSz > GetLength()) return false;
		bool last = (l + naluSz >= GetLength());
		PacketizeH264Nalu(mtu, l, naluSz, last);
		l += naluSz;
	}
	return true;
}

void VideoFrame::PacketizeH264Nalu(unsigned int mtu, DWORD offset, DWORD naluSz, bool last)
{
	BYTE * p = GetData();
	p += offset;
	unsigned int l = 0;

	if (naluSz <= mtu)
	{
		AddRtpPacket(l, naluSz, 0L, 0, last);
		return;
	}

	uint8_t fua_hdr[H264_FUA_HEADER_SIZE];
	fua_hdr[0] = p[l] & 0x60; /* NRI */
	fua_hdr[0] |= 28;          /* fu_a */
	fua_hdr[1] = 0x80;         /* S=1,E=0,R=0 */
	fua_hdr[1] |= p[l] & 0x1f; /* type */

	while (l < naluSz)
	{
		unsigned long pktSize = naluSz - l;
		if (pktSize > mtu)
			pktSize = mtu;
		else
			fua_hdr[1] |= 0x40; /* E bit */

		AddRtpPacket(offset + l, pktSize, fua_hdr, H264_FUA_HEADER_SIZE,
			     pktSize + l >= naluSz);

		fua_hdr[1] &= 0x7F; /* clear S bit */
		l += pktSize;
	}
}

bool VideoFrame::PacketizeH263(unsigned int mtu)
{
	// Fragmentation RFC 2429 : chaque fragment précédé de 2 octets d'en-tête.
	// Premier paquet : bit P=1 (début d'image), suivants P=0.
	const unsigned int H263P_HEADER = 2;
	unsigned int payload = (mtu > H263P_HEADER) ? (mtu - H263P_HEADER) : 1;

	ClearRTPPacketizationInfo();

	BYTE prefix[H263P_HEADER];
	bool first = true;

	for (unsigned int i = 0; i < GetLength(); i += payload)
	{
		unsigned int len = GetLength() - i;
		bool last = (len <= payload);
		if (!last) len = payload;

		prefix[0] = first ? 0x04 : 0x00; /* P=1 sur le premier fragment */
		prefix[1] = 0x00;

		AddRtpPacket(i, len, prefix, H263P_HEADER, last);
		first = false;
	}
	return true;
}
