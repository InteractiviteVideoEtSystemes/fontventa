/**
 * test_codec_catalogue.cpp — consistency of the codec catalogue (codecs.cpp)
 * with the installed ffmpeg.
 *
 * ffmpeg may decode a codec it cannot encode: this depends on the libraries
 * built into the package (libopencore-amr, libgsm, libspeex, libx264...). A
 * package missing one of them leaves IsSupported() true and
 * IsEncodingSupported() false, and the failure only shows up when the encoder
 * is created. These tests surface it here.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/codecs.h>
#include <vector>
#include <cmath>
#include <memory>

namespace {

SamplesPtr MakeToneSamples(int nb, int rate)
{
	std::vector<SWORD> pcm(nb);
	for (int i = 0; i < nb; i++)
		pcm[i] = (SWORD)(8000.0 * sin(2.0 * M_PI * 440.0 * i / rate));
	return Samples::FromBuffer(&pcm[0], (DWORD)nb, (DWORD)rate);
}

} // namespace

TEST(AudioCatalogue, ToutCodecRecuSaitAussiEtreEmis)
{
	for (AudioCodec::Type t : AudioCodecFactory::GetSupportedCodecs())
		EXPECT_TRUE(AudioCodec::IsEncodingSupported(t))
			<< AudioCodec::GetNameFor(t) << " : décodeur présent, encodeur absent de ffmpeg";
}

TEST(AudioCatalogue, ChaqueEncodeurAnnonceProduitDesTrames)
{
	for (AudioCodec::Type t : AudioCodecFactory::GetSupportedEncoderCodecs())
	{
		if (t == AudioCodec::TELEPHONE_EVENT || t == AudioCodec::SLIN)
			continue;

		Properties props;
		std::unique_ptr<AudioEncoder> enc(AudioCodecFactory::CreateEncoder(t, props));
		ASSERT_TRUE(enc != nullptr) << AudioCodec::GetNameFor(t);

		DWORD rate = enc->TrySetRate(8000);
		if (rate == 0)
			rate = 8000;

		int frames = 0;
		for (int k = 0; k < 50 && frames == 0; k++)
			for (AudioFramePtr f = enc->EncodeFrame(MakeToneSamples(rate / 50, rate)); f; f = enc->EncodeFrame(nullptr))
				frames++;

		EXPECT_GT(frames, 0) << AudioCodec::GetNameFor(t) << " : une seconde de signal n'a donné aucune trame";
	}
}

TEST(VideoCatalogue, ToutCodecRecuSaitAussiEtreEmis)
{
	for (VideoCodec::Type t : VideoCodecFactory::GetSupportedCodecs())
	{
		// VP6: decode only, by design (see codecs.h).
		if (t == VideoCodec::VP6)
			continue;
		EXPECT_TRUE(VideoCodec::IsEncodingSupported(t))
			<< VideoCodec::GetNameFor(t) << " : décodeur présent, encodeur absent de ffmpeg";
	}
}
