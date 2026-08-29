/**
 * test_vp8_realtime.cpp — l'encodeur VP8 tient le temps réel en 720p.
 *
 * Appel du 2026-08-29 : libvpx ouvert avec les défauts ffmpeg (deadline
 * « good », un thread) mettait ~96 ms par image 720p sur une machine à deux
 * cœurs, pour un budget de 50 ms à 20 im/s. Le transcodeur inline, qui encode
 * sur le thread de démux de la source, prenait du retard, et la jambe RTP
 * jetait ses paquets.
 *
 * Ce test mesure le coût par image et le borne. La borne est large — la moitié
 * du budget d'une source à 15 im/s — pour ne pas dépendre de la machine ; elle
 * reste bien en dessous de ce que coûte le mode « good ».
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <medkit/tools.h>
#include <vp8/vp8encoder.h>

namespace {

const int W = 1280, H = 720;

// Image texturée déterministe : un dégradé bruité, pour que l'encodeur ait
// quelque chose à coder sans que la trame soit du bruit pur (incompressible,
// donc atypiquement lente).
PictPtr CreateTextured(int n)
{
	PictPtr pic = Pict::CreateColor(W, H, 128, 128, 128);
	if (!pic || !pic->GetAVFrame())
		return nullptr;
	AVFrame* f = pic->GetAVFrame();
	DWORD seed = 12345u + (DWORD)n * 7919u;
	for (int y = 0; y < H; y++)
	{
		BYTE* line = f->data[0] + y * f->linesize[0];
		for (int x = 0; x < W; x++)
		{
			seed = seed * 1103515245u + 12345u;
			line[x] = (BYTE)(((x + n * 3) * 255 / W + (seed >> 28)) & 0xFF);
		}
	}
	return pic;
}

TEST(Vp8Realtime, UneImage720pCouteMoinsDe33ms)
{
	Properties props;
	VP8Encoder enc(props);
	ASSERT_EQ(1, enc.SetFrameRate(20, 2500, 200));
	ASSERT_EQ(1, enc.SetSize(W, H));

	const int frames = 40;
	QWORD total = 0;
	QWORD worst = 0;
	int encoded = 0;

	for (int n = 0; n < frames; n++)
	{
		PictPtr pic = CreateTextured(n);
		ASSERT_TRUE(pic);

		const QWORD before = getTime();
		VideoFrame* out = enc.EncodeFrame(pic);
		const QWORD cost = getTime() - before;

		if (!out)
			continue;
		encoded++;
		// La première image porte l'ouverture du codec et une trame clé :
		// on la mesure mais on ne la juge pas.
		if (n == 0)
			continue;
		total += cost;
		if (cost > worst)
			worst = cost;
	}

	ASSERT_GT(encoded, frames / 2);
	const double avgMs = (double)total / 1000.0 / (encoded - 1);
	Log("-Vp8Realtime: %d images 720p, %.1f ms par image en moyenne, pire %.1f ms\n",
	    encoded, avgMs, worst / 1000.0);

	EXPECT_LT(avgMs, 33.0)
		<< "l'encodeur VP8 doit tenir le temps reel en 720p : deadline realtime, cpu-used, threads";
}

}	// namespace
