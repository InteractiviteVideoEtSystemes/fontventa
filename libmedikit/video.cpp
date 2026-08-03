#include "medkit/log.h"
#include "medkit/video.h"
extern "C"
{
#include <libavutil/hwcontext.h>
}
#include "h263/h263codec.h"
#include "h264/h264encoder.h"
#include "h264/h264decoder.h"
#include "vp8/vp8decoder.h"
#include "vp8/vp8encoder.h"
#include "av1/av1codec.h"
#include "ffvideocodec.h"


PictPtr Pict::DownloadToCPU() const
{
	// Rien à faire si la trame n'est pas une surface GPU (déjà CPU, ou vide) :
	// le consommateur est censé tester IsGPUPict() avant d'appeler.
	if (!av_frame || av_frame->format != AV_PIX_FMT_VAAPI)
		return nullptr;

	AVFrame *sw = av_frame_alloc();
	if (!sw)
		return nullptr;
	sw->format = AV_PIX_FMT_YUV420P;
	sw->width  = av_frame->width;
	sw->height = av_frame->height;

	int ret = av_frame_get_buffer(sw, 0);
	if (ret < 0)
	{
		Error("-Pict::DownloadToCPU: av_frame_get_buffer failed (%d)\n", ret);
		av_frame_free(&sw);
		return nullptr;
	}

	ret = av_hwframe_transfer_data(sw, av_frame, 0);
	if (ret < 0)
	{
		Error("-Pict::DownloadToCPU: av_hwframe_transfer_data failed (%d)\n", ret);
		av_frame_free(&sw);
		return nullptr;
	}

	// Conserve pts / métadonnées de la trame source.
	av_frame_copy_props(sw, av_frame);
	return std::make_shared<Pict>(sw);
}

// Contexte de device VAAPI partagé, créé au plus une fois (thread-safe : magic
// static C++11). Renvoie nullptr si l'accélération VAAPI n'est pas disponible.
static AVBufferRef* GetSharedVAAPIDevice()
{
	static AVBufferRef* device = []() -> AVBufferRef*
	{
		AVBufferRef* dev = nullptr;
		int ret = av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
		if (ret < 0)
		{
			Error("-VAAPI device unavailable, GPU upload disabled (%d)\n", ret);
			return nullptr;
		}
		return dev;
	}();
	return device;
}

// Accesseur public du device partagé (cf. medkit/video.h) : décodeurs,
// encodeurs et graphes de composition doivent TOUS dériver de ce device.
AVBufferRef* Pict::GetVAAPIDevice()
{
	return GetSharedVAAPIDevice();
}

int Pict::UploadToGPU(PictPtr& out) const
{
	out = nullptr;

	if (!av_frame)
		return AVERROR(EINVAL);

	// Déjà une surface GPU : le consommateur est censé tester IsGPUPict() avant.
	if (av_frame->format == AV_PIX_FMT_VAAPI)
		return AVERROR(EALREADY);

	// Device VAAPI partagé — absent => pas d'accélération sur cette machine.
	AVBufferRef* device = GetSharedVAAPIDevice();
	if (!device)
		return AVERROR(ENOSYS);

	// Contexte de frames GPU pour cette taille. sw_format = format de la trame
	// source (av_hwframe_transfer_data ne convertit pas le pixel format).
	AVBufferRef* frames_ref = av_hwframe_ctx_alloc(device);
	if (!frames_ref)
		return AVERROR(ENOMEM);

	AVHWFramesContext* frames_ctx = (AVHWFramesContext*) frames_ref->data;
	frames_ctx->format            = AV_PIX_FMT_VAAPI;
	frames_ctx->sw_format         = (AVPixelFormat) av_frame->format;
	frames_ctx->width             = av_frame->width;
	frames_ctx->height            = av_frame->height;
	frames_ctx->initial_pool_size = 1;

	int ret = av_hwframe_ctx_init(frames_ref);
	if (ret < 0)
	{
		Error("-Pict::UploadToGPU: av_hwframe_ctx_init failed (%d)\n", ret);
		av_buffer_unref(&frames_ref);
		return ret;
	}

	AVFrame* hw = av_frame_alloc();
	if (!hw)
	{
		av_buffer_unref(&frames_ref);
		return AVERROR(ENOMEM);
	}

	// Alloue une surface GPU dans le pool.
	ret = av_hwframe_get_buffer(frames_ref, hw, 0);
	if (ret < 0)
	{
		Error("-Pict::UploadToGPU: av_hwframe_get_buffer failed (%d)\n", ret);
		av_frame_free(&hw);
		av_buffer_unref(&frames_ref);
		return ret;
	}

	// Upload CPU->GPU.
	ret = av_hwframe_transfer_data(hw, av_frame, 0);
	if (ret < 0)
	{
		Error("-Pict::UploadToGPU: av_hwframe_transfer_data failed (%d)\n", ret);
		av_frame_free(&hw);
		av_buffer_unref(&frames_ref);
		return ret;
	}

	av_frame_copy_props(hw, av_frame);
	// La trame conserve sa propre référence au frames_ctx (hw->hw_frames_ctx) :
	// on relâche notre référence locale.
	av_buffer_unref(&frames_ref);

	out = std::make_shared<Pict>(hw);
	return 0;
}

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
		case VideoCodec::AV1:
			return new AV1Decoder();
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
		case VideoCodec::AV1:
			return new AV1Encoder(properties);
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

	// Saut d'un éventuel start code de tête : UNIQUEMENT en mode Annex-B.
	// En AVCC (useStartCode==false), les 4 premiers octets sont un préfixe de
	// longueur ; un préfixe valant 00 00 01 xx (taille dans [256,512[) NE doit
	// PAS être confondu avec un start code.
	if (useStartCode)
	{
		if (p[l] == 0 && p[l+1] == 0)
		{
			if (p[l+2] == 1)
				l += 3;
		}
		else if(p[l+2] == 0 && p[l+3] == 1)
		{
			l += 4;
		}
	}

	while (l < GetLength())
	{
		if (useStartCode)
		{
			naluSz = DetectNaluBoundary(p + l, GetLength() - l);
			if (naluSz == 0 || naluSz > GetLength()) return false;
		}
		else
		{
			// AVCC : lire la taille puis SAUTER le préfixe de longueur avant
			// de packetiser la NALU elle-même.
			naluSz = ReadNaluSize(p + l);
			l += naluSizeLen;
			if (naluSz == 0 || l + naluSz > GetLength()) return false;
		}

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
		// NALU tenant dans un seul paquet : référencer la NALU à `offset`
		// (et non l, qui vaut 0 → pointerait sur le préfixe de longueur).
		AddRtpPacket(offset, naluSz, 0L, 0, last);
		return;
	}

	uint8_t fua_hdr[H264_FUA_HEADER_SIZE];
	fua_hdr[0] = p[0] & 0x60; /* NRI */
	fua_hdr[0] |= 28;          /* fu_a */
	fua_hdr[1] = 0x80;         /* S=1,E=0,R=0 */
	fua_hdr[1] |= p[0] & 0x1f; /* type */

	/* La charge utile FU-A EXCLUT l'octet d'en-tête NAL : le récepteur le
	 * reconstruit depuis l'indicateur et l'en-tête FU (RFC 6184 §5.8).
	 * L'inclure (l=0) le dupliquait à la réception -- toute la slice décalée
	 * d'un octet, IDR indécodable dès le 3e macrobloc (constaté en production
	 * sur tout NAL fragmenté ; les NAL <= mtu, non fragmentés, étaient sains). */
	l = 1;

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
