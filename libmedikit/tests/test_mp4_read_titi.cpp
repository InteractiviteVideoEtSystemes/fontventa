/**
 * test_mp4_read_titi.cpp — Mp4FfReader sur la fixture titi.mp4 (H264 640x360 +
 * AAC 44.1 kHz stéréo, ~45 s, sans piste texte).
 *
 * Contrairement à record.mp4 (flux H264 défectueux, cf. libmedikit_tests_plan.md),
 * titi.mp4 a un flux H264 sain : on teste ici la LECTURE VIDÉO réelle
 * (GetNextFrame produit des trames vidéo, dont au moins une intra) en plus des
 * métadonnées.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/ffmp4reader.h>
#include <memory>
#include <unistd.h>

#ifndef TEST_MP4_TITI_FILE
#define TEST_MP4_TITI_FILE "fixtures/titi.mp4"
#endif

namespace {
std::unique_ptr<Mp4FfReader> OpenTitiOrSkip()
{
	auto r = std::make_unique<Mp4FfReader>(TEST_MP4_TITI_FILE);
	if (!r->IsOpen())
		return nullptr;
	return r;
}
} // namespace

// --- Métadonnées vidéo ------------------------------------------------------
TEST(Mp4ReadTiti, MetadonneesVideo)
{
	auto r = OpenTitiOrSkip();
	if (!r) GTEST_SKIP() << "fixture absente : " << TEST_MP4_TITI_FILE;

	VideoCodec::Type vc[] = { VideoCodec::H264, VideoCodec::VP8 };
	ASSERT_GT(r->OpenTrack(vc, 2, VideoCodec::H264, false, false), 0);
	EXPECT_TRUE(r->HasVideoTrack());

	VideoCodec::Type got;
	ASSERT_TRUE(r->GetVideoCodec(got));
	EXPECT_EQ(got, VideoCodec::H264);
	EXPECT_EQ(r->GetVideoWidth(),  640u);
	EXPECT_EQ(r->GetVideoHeight(), 360u);
	EXPECT_NEAR(r->GetDuration(), 45.04, 0.5);
}

// --- Lecture vidéo réelle (flux H264 sain) ----------------------------------
// On lit en respectant le cadencement jusqu'à obtenir plusieurs trames vidéo,
// dont au moins une intra ; aucun errcode dur ne doit survenir.
TEST(Mp4ReadTiti, LectureVideoReelle)
{
	auto r = OpenTitiOrSkip();
	if (!r) GTEST_SKIP() << "fixture absente : " << TEST_MP4_TITI_FILE;

	VideoCodec::Type vc[] = { VideoCodec::H264 };
	ASSERT_GT(r->OpenTrack(vc, 1, VideoCodec::H264, false, false), 0);

	const int kTarget = 10;          // s'arrêter dès 10 trames vidéo (test rapide)
	int nVideo = 0, nIntra = 0, err = 0;
	unsigned long wait = 0;
	r->Rewind();

	for (int i = 0; i < 4000 && nVideo < kTarget; i++)
	{
		MediaFrame* f = r->GetNextFrame(err, wait);
		if (err == -1) break;                        // EOF
		ASSERT_GE(err, 0) << "GetNextFrame errcode=" << err;
		if (f && f->GetType() == MediaFrame::Video)
		{
			nVideo++;
			if (((VideoFrame*)f)->IsIntra()) nIntra++;
		}
		if (wait > 0) usleep(wait * 1000);           // respecter le cadencement
	}

	EXPECT_GE(nVideo, kTarget) << "trop peu de trames vidéo relues";
	EXPECT_GE(nIntra, 1)       << "aucune trame intra relue";
}
