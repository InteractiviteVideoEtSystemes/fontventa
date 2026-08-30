/**
 * test_encoder_frame_lifetime.cpp — la trame rendue par EncodeFrame appartient
 * à celui qui la tient, pas à l'encodeur.
 *
 * Contrat : une trame encodée survit à tout ce qui arrive ensuite à l'encodeur
 * (réouverture par SetFrameRate, EncodeFrame suivant). Rouge tant que
 * l'encodeur recycle un membre unique (crash du 2026-08-30 : videostream
 * rouvrait le codec entre EncodeFrame et l'envoi, puis émettait une trame
 * libérée). À jouer sous ASAN=yes pour que la lecture après libération soit
 * une erreur ferme et non une comparaison de valeurs.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <medkit/audio.h>
#include <vp8/vp8encoder.h>
#include <vector>
#include <memory>

namespace {

const int W = 320, H = 240;

PictPtr CreateNoise(DWORD& seed)
{
	PictPtr pic = Pict::CreateColor(W, H, 128, 128, 128);
	if (!pic || !pic->GetAVFrame())
		return nullptr;
	AVFrame* f = pic->GetAVFrame();
	for (int p = 0; p < 3; p++)
	{
		int w = p ? W / 2 : W;
		int h = p ? H / 2 : H;
		for (int y = 0; y < h; y++)
		{
			BYTE* line = f->data[p] + y * f->linesize[p];
			for (int x = 0; x < w; x++)
			{
				seed = seed * 1103515245u + 12345u;
				line[x] = (BYTE)(96 + ((seed >> 16) & 0x3F));
			}
		}
	}
	return pic;
}

SamplesPtr MakeTone(int nb, int rate, int& phase)
{
	std::vector<int16_t> pcm(nb);
	for (int i = 0; i < nb; i++, phase++)
		pcm[i] = (int16_t)(8000 * ((phase / 20) % 2 ? 1 : -1));
	return Samples::FromBuffer(&pcm[0], (DWORD)nb, (DWORD)rate);
}

struct Snapshot
{
	MediaFrame::Type type;
	DWORD length;
	size_t packets;
	std::vector<BYTE> head;
};

Snapshot Take(const MediaFrame& f)
{
	Snapshot s;
	s.type = f.GetType();
	s.length = f.GetLength();
	s.packets = const_cast<MediaFrame&>(f).GetRtpPacketizationInfo().size();
	DWORD n = s.length < 64 ? s.length : 64;
	s.head.assign(f.GetData(), f.GetData() + n);
	return s;
}

} // namespace

TEST(EncoderFrameLifetime, LaTrameVideoSurvitALaReouverture)
{
	DWORD seed = 7;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 10000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	VideoFramePtr kept;
	for (int i = 0; i < 5 && !kept; i++)
		kept = enc.EncodeFrame(CreateNoise(seed));
	ASSERT_NE(kept, nullptr);
	Snapshot before = Take(*kept);
	ASSERT_GT(before.length, 0u);
	ASSERT_GT(before.packets, 0u);

	// Consigne divisée par 5 : ShouldReopenForBitrate rouvre le codec.
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);

	Snapshot after = Take(*kept);
	EXPECT_EQ(after.type, MediaFrame::Video);
	EXPECT_EQ(after.length, before.length);
	EXPECT_EQ(after.packets, before.packets);
	EXPECT_EQ(after.head, before.head);
}

TEST(EncoderFrameLifetime, DeuxTramesVideoSontDeuxObjets)
{
	DWORD seed = 8;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	VideoFramePtr first;
	for (int i = 0; i < 5 && !first; i++)
		first = enc.EncodeFrame(CreateNoise(seed));
	ASSERT_NE(first, nullptr);
	Snapshot before = Take(*first);

	VideoFramePtr second;
	for (int i = 0; i < 5 && !second; i++)
		second = enc.EncodeFrame(CreateNoise(seed));
	ASSERT_NE(second, nullptr);

	EXPECT_NE(first, second);
	Snapshot after = Take(*first);
	EXPECT_EQ(after.length, before.length);
	EXPECT_EQ(after.head, before.head);
}

TEST(EncoderFrameLifetime, LaTrameAudioSurvitALaTrameSuivante)
{
	std::unique_ptr<AudioEncoder> enc(AudioCodecFactory::CreateEncoder(AudioCodec::OPUS));
	ASSERT_NE(enc, nullptr);
	int phase = 0;
	const int rate = (int)enc->GetClockRate();

	AudioFramePtr first;
	for (int i = 0; i < 10 && !first; i++)
		first = enc->EncodeFrame(MakeTone(rate / 50, rate, phase));
	ASSERT_NE(first, nullptr);
	Snapshot before = Take(*first);
	ASSERT_GT(before.length, 0u);

	AudioFramePtr second;
	for (int i = 0; i < 10 && !second; i++)
		second = enc->EncodeFrame(MakeTone(rate / 50, rate, phase));
	ASSERT_NE(second, nullptr);

	EXPECT_NE(first, second);
	Snapshot after = Take(*first);
	EXPECT_EQ(after.type, MediaFrame::Audio);
	EXPECT_EQ(after.length, before.length);
	EXPECT_EQ(after.head, before.head);
}
