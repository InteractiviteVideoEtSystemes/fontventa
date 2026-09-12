/**
 * test_av1_encoder_concurrency.cpp — plusieurs encodeurs AV1 s'ouvrent et se
 * ferment en même temps sans verrou côté libmedkit.
 *
 * SVT-AV1 0.9.0 tenait son inventaire de processeurs dans un global sans
 * verrou : deux pattes qui ouvraient ou fermaient leur encodeur à quelques
 * millisecondes d'écart faisaient déréférencer NULL (SIGSEGV en trafic réel,
 * 2026-08-13). Le paquet ffmpeg 9 IVèS embarque SVT-AV1 4.2.0, dont le
 * changelog 4.1 ajoute les mutex nécessaires ; libmedkit ne sérialise donc
 * plus rien. Ce test reproduit le scénario : si la course revient avec un
 * futur paquet, il plante.
 */
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <av1/av1codec.h>

namespace {

const int W = 320, H = 240;

void OpenEncodeClose(int cycles, std::atomic<int>& opened)
{
	for (int c = 0; c < cycles; c++)
	{
		Properties props;
		props.SetProperty("av1.preset", "12");
		AV1Encoder enc(props);
		if (enc.SetFrameRate(15, 500, 30) != 1 || enc.SetSize(W, H) != 1)
			continue;
		opened++;
		for (int n = 0; n < 3; n++)
		{
			PictPtr pic = Pict::CreateColor(W, H, (BYTE)(40 * n + c), 128, 128);
			enc.EncodeFrame(pic);
		}
	}
}

TEST(Av1EncoderConcurrency, OuverturesEtFermeturesSimultanees)
{
	if (!AV1Encoder::IsSupported())
		GTEST_SKIP() << "libsvtav1 absent du ffmpeg installé";

	const int threads = 4;
	const int cycles  = 15;
	std::atomic<int> opened(0);
	std::vector<std::thread> pool;
	for (int t = 0; t < threads; t++)
		pool.emplace_back(OpenEncodeClose, cycles, std::ref(opened));
	for (auto& th : pool)
		th.join();

	EXPECT_EQ(threads * cycles, opened.load());
}

} // namespace
