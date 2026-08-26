#include "medkit/log.h"
#include "medkit/audio.h"
#include <string.h>
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}
#include "g711/g711codec.h"
#include "g722/g722codec.h"
#include "aac/aacencoder.h"
#include "aac/aacdecoder.h"
#include "amr/amrcodec.h"
#include "nelly/nellycodec.h"
#include "gsm/gsmcodec.h"
#include "opus/opuscodec.h"
#include "speex/speexcodec.h"



AudioEncoder* AudioCodecFactory::CreateEncoder(AudioCodec::Type codec)
{
	//Empty properties
	Properties properties;

	//Create codec
	return CreateEncoder(codec,properties);
}

AudioEncoder* AudioCodecFactory::CreateEncoder(AudioCodec::Type codec, const Properties &properties)
{
	Log("-CreateAudioEncoder [%d,%s]\n",codec,AudioCodec::GetNameFor(codec));

	//Creamos uno dependiendo del tipo
	switch(codec)
	{
		case AudioCodec::PCMA:
			return new PCMAEncoder(properties);
		case AudioCodec::PCMU:
			return new PCMUEncoder(properties);

		case AudioCodec::G722:
			return new G722Encoder(properties);

		case AudioCodec::AAC:
			return new AACEncoder(properties);

		case AudioCodec::AMR:
			return new AMRNBEncoder(properties);

		case AudioCodec::AMRWB:
			return new AMRWBEncoder(properties);

		case AudioCodec::NELLY8:
			return new NellyEncoder(properties);
		case AudioCodec::NELLY11:
			return new NellyEncoder11Khz(properties);

		case AudioCodec::GSM:
			return new GSMEncoder(properties);

		case AudioCodec::SPEEX16:
			return new SpeexEncoder(properties);

		case AudioCodec::OPUS:
			return new OPUSEncoder(properties);

		default:
			Error("Codec not found [%d]\n",codec);
	}

	return NULL;
}

AudioDecoder* AudioCodecFactory::CreateDecoder(AudioCodec::Type codec)
{
	Log("-CreateAudioDecoder [%d,%s]\n",codec,AudioCodec::GetNameFor(codec));

	//Creamos uno dependiendo del tipo
	switch(codec)
	{

		case AudioCodec::G722:
			return new G722Decoder();
		case AudioCodec::AMR:
			return new AMRNBDecoder();
		case AudioCodec::AMRWB:
			return new AMRWBDecoder();
		case AudioCodec::PCMA:
			return new PCMADecoder();
		case AudioCodec::PCMU:
			return new PCMUDecoder();
		case AudioCodec::NELLY8:
			return new NellyDecoder();
		case AudioCodec::NELLY11:
			return new NellyDecoder11Khz();

		case AudioCodec::GSM:
			return new GSMDecoder();

		case AudioCodec::SPEEX16:
			return new SpeexDecoder();

		case AudioCodec::OPUS:
			return new OPUSDecoder();

		case AudioCodec::AAC:
			// AAC des MP4 : nécessite l'extradata (cf. surcharge à 3 arguments).
			return new AACDecoder();

		default:
			Error("Codec not found [%d]\n",codec);
	}

	return NULL;
}

AudioDecoder* AudioCodecFactory::CreateDecoder(AudioCodec::Type codec, const BYTE* extradata, DWORD extradataSize)
{
	// Seul l'AAC exploite l'extradata (AudioSpecificConfig des MP4) ; les autres
	// codecs n'en ont pas besoin et passent par la fabrique standard.
	if (codec == AudioCodec::AAC)
		return new AACDecoder(extradata, (int)extradataSize);
	return CreateDecoder(codec);
}

/******************************************************************************
 *                                  Samples                                   *
 ******************************************************************************/

bool Samples::IsS16Mono(const AVFrame * frame)
{
	return frame
		&& frame->format == AV_SAMPLE_FMT_S16
		&& frame->ch_layout.nb_channels == 1
		&& frame->data[0] != nullptr;
}

SamplesPtr Samples::Alloc(DWORD nb, DWORD rate)
{
	if (nb == 0 || rate == 0)
		return nullptr;

	AVFrame * frame = av_frame_alloc();
	if (!frame)
		return nullptr;

	frame->format      = AV_SAMPLE_FMT_S16;
	frame->sample_rate = (int)rate;
	frame->nb_samples  = (int)nb;
	frame->pts         = AV_NOPTS_VALUE;
	av_channel_layout_default(&frame->ch_layout, 1);

	if (av_frame_get_buffer(frame, 0) < 0)
	{
		av_frame_free(&frame);
		return nullptr;
	}

	return std::make_shared<Samples>(frame);
}

SamplesPtr Samples::FromAVFrame(AVFrame * frame)
{
	// Refus SANS libérer : la trame appartient encore à l'appelant tant que
	// l'adoption n'a pas eu lieu, c'est à lui de la défaire.
	if (!IsS16Mono(frame))
		return nullptr;

	return std::make_shared<Samples>(frame);
}

SamplesPtr Samples::FromBuffer(const SWORD * buffer, DWORD nb, DWORD rate)
{
	if (!buffer)
		return nullptr;

	SamplesPtr samples = Alloc(nb, rate);
	if (!samples)
		return nullptr;

	memcpy(samples->GetData(), buffer, (size_t)nb * sizeof(SWORD));
	return samples;
}

/******************************************************************************
 *          Adaptateurs transitoires vers l'ancienne interface plate          *
 *                                                                            *
 * Ils vivent ici, en UN exemplaire, le temps que les chaînes du mcu passent   *
 * à SamplesPtr (design/audio-avframe.md §6). Ils bornent l'écriture, ce que   *
 * les appelants ne faisaient pas : c'est déjà la classe de bug du 14/08 qui   *
 * disparaît, même avant migration de l'appelant.                             *
 ******************************************************************************/

int AudioEncoder::Encode(SWORD *in, int inLen, BYTE* out, int outLen)
{
	if (inLen <= 0 || !in || !out)
		return 0;

	// Fréquence à laquelle l'appelant fournit : celle négociée par TrySetRate,
	// à défaut celle du codec.
	DWORD rate = inputRate ? inputRate : GetRate();
	if (rate == 0)
		return Error("-AudioEncoder: rate unknown, TrySetRate() missing\n");

	SamplesPtr samples = Samples::FromBuffer(in, (DWORD)inLen, rate);
	if (!samples)
		return Error("-AudioEncoder: could not allocate %d samples\n", inLen);

	// Une seule trame par appel : si l'entrée en contenait plusieurs, le reste
	// demeure dans la fifo de l'encodeur et sortira aux appels suivants.
	AudioFrame * frame = EncodeFrame(samples);
	if (!frame)
		return 0;

	if ((int)frame->GetLength() > outLen)
		return Error("-AudioEncoder: output buffer too small [%u>%d]\n",
			frame->GetLength(), outLen);

	memcpy(out, frame->GetData(), frame->GetLength());
	return (int)frame->GetLength();
}

int AudioDecoder::Decode(BYTE *in, int inLen, SWORD* out, int outLen)
{
	if (!out || outLen <= 0)
		return 0;

	if (in && inLen > 0)
		Decode(in, inLen);

	// Reprise de la trame entamée à l'appel précédent, sinon la suivante.
	if (!pending || pendingOffset >= pending->GetNbSamples())
	{
		pending = GetFrame();
		pendingOffset = 0;
	}

	if (!pending)
		return 0;

	DWORD avail = pending->GetNbSamples() - pendingOffset;
	DWORD want  = (avail > (DWORD)outLen) ? (DWORD)outLen : avail;

	memcpy(out, pending->GetData() + pendingOffset, (size_t)want * sizeof(SWORD));
	pendingOffset += want;

	if (pendingOffset >= pending->GetNbSamples())
		pending.reset();

	return (int)want;
}

bool AudioFrame::Packetize(unsigned int mtu)
{
	unsigned int paksize = packetization;
	if (paksize > mtu && mtu > 0) paksize = mtu;

	ClearRTPPacketizationInfo();
	for (unsigned int i=0; i<GetLength(); i+= paksize )
	{
		unsigned int rtplen = GetLength() - i;

		if (rtplen > paksize ) rtplen = paksize;
		AddRtpPacket(i, rtplen, NULL, 0, false);
	}
	return true;
}
