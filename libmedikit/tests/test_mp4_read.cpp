/**
 * test_mp4_read.cpp — Mp4FfReader sur la fixture réelle record.mp4
 * (tests/fixtures/record.mp4, chemin injecté par -DTEST_MP4_FILE).
 *
 * Couvre le chemin démux + métadonnées (ouverture, sélection de piste,
 * détection de codec, dimensions, durée, avcC) et le contrat de codec audio
 * AAC. Ces valeurs sont déterministes (cf. ffprobe : H264 640x480, AAC 48 kHz,
 * mov_text, ~16.77 s).
 *
 * NOTE : la lecture VIDÉO cadencée (GetNextFrame) de ce fichier échoue à la
 * packetisation H264 (« Invalid NAL unit size ») dès la 1ʳᵉ trame intra — voir
 * libmedikit_tests_plan.md §anomalie. On ne teste donc pas ici la boucle de
 * lecture vidéo ; ce point est suivi séparément.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/ffmp4reader.h>
#include <memory>

#ifndef TEST_MP4_FILE
#define TEST_MP4_FILE "fixtures/record.mp4"
#endif

namespace {
// Ouvre la fixture ou skippe proprement si elle est absente (build sans fixture).
std::unique_ptr<Mp4FfReader> OpenFixtureOrSkip()
{
	auto r = std::make_unique<Mp4FfReader>(TEST_MP4_FILE);
	if (!r->IsOpen())
		return nullptr;
	return r;
}
} // namespace

// --- Métadonnées vidéo + avcC + piste texte ---------------------------------
TEST(Mp4Read, MetadonneesVideoEtTexte)
{
	auto r = OpenFixtureOrSkip();
	if (!r) GTEST_SKIP() << "fixture absente : " << TEST_MP4_FILE;

	VideoCodec::Type vc[] = { VideoCodec::H264, VideoCodec::VP8 };
	ASSERT_GT(r->OpenTrack(vc, 2, VideoCodec::H264, false, false), 0);
	ASSERT_EQ(r->OpenTrack(TextCodec::T140, 106, 1), 1);

	EXPECT_TRUE(r->HasVideoTrack());
	EXPECT_TRUE(r->HasTextTrack());

	VideoCodec::Type got;
	ASSERT_TRUE(r->GetVideoCodec(got));
	EXPECT_EQ(got, VideoCodec::H264);
	EXPECT_EQ(r->GetVideoWidth(),  640u);
	EXPECT_EQ(r->GetVideoHeight(), 480u);

	// Durée ~16.77 s (tolérance large : cadencement/arrondi).
	EXPECT_NEAR(r->GetDuration(), 16.77, 0.5);

	// avcC : version 1, profil Baseline (0x42), 1 SPS + 1 PPS, NAL length = 4.
	AVCDescriptor* d = r->GetAVCDescriptor();
	ASSERT_TRUE(d != NULL);
	EXPECT_EQ(d->GetConfigurationVersion(), 1);
	EXPECT_EQ(d->GetAVCProfileIndication(), 0x42);
	EXPECT_EQ(d->GetNumOfSequenceParameterSets(), 1);
	EXPECT_EQ(d->GetNumOfPictureParameterSets(), 1);
	EXPECT_EQ(d->GetNALUnitLength(), 3); // stocké = taille-1 (=> 4 octets)
	delete d;
}

// --- Contrat de codec audio (AAC) -------------------------------------------
// L'audio du fichier est de l'AAC : hors des codecs télécom passthrough, mais
// sélectionnable si AAC est explicitement proposé, et transcodable vers PCMU.
TEST(Mp4Read, ContratAudioAac)
{
	// (a) passthrough SANS AAC dans la liste -> refusé (0).
	{
		auto r = OpenFixtureOrSkip();
		if (!r) GTEST_SKIP() << "fixture absente";
		AudioCodec::Type tel[] = { AudioCodec::PCMU, AudioCodec::PCMA,
		                           AudioCodec::G722, AudioCodec::OPUS };
		EXPECT_EQ(r->OpenTrack(tel, 4, AudioCodec::PCMU, false), 0);
	}
	// (b) transcodage AAC -> PCMU accepté (1).
	{
		auto r = OpenFixtureOrSkip();
		if (!r) GTEST_SKIP() << "fixture absente";
		EXPECT_EQ(r->OpenAudioTranscoded(AudioCodec::PCMU), 1);
		EXPECT_TRUE(r->HasAudioTrack());
	}
}
