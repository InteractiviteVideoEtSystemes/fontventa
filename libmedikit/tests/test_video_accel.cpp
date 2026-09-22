/**
 * test_video_accel.cpp — ce que l'accélération vidéo fait réellement.
 *
 * Deux mécanismes, tous deux invisibles sans ces compteurs :
 *
 *  - `VideoAccel` compte les codecs vidéo OUVERTS et ceux qui tournent en
 *    VAAPI. Savoir qu'un device existe ne dit pas si un encodeur donné s'en
 *    sert : le repli logiciel est silencieux par conception
 *    (FfVideoEncoder::FallbackToSoftware) ;
 *  - `Pict::DisableVAAPI` éteint l'accélération pour tout le processus, ce qui
 *    permet de rejouer la même charge sans GPU sans désinstaller le driver.
 *
 * Ces tests tournent AVEC ou SANS GPU : ils assertent le contrat des deux
 * côtés, et la branche empruntée dépend de `Pict::GetVAAPIDevice()`.
 *
 * `DisableVAAPI` est IRRÉVERSIBLE et globale au processus : la tester en place
 * éteindrait le GPU de tous les tests joués après elle. D'où le sous-processus.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <h264/h264encoder.h>
#include <h264/h264decoder.h>
#include <ffvideocodec.h>

// ===========================================================================
// Compteurs d'encodeurs et de décodeurs
// ===========================================================================

TEST(VideoAccel, EncoderIsCountedWhileOpen)
{
	VideoAccelStats before = VideoAccel::GetStats();

	{
		Properties props;
		H264Encoder enc(props);
		enc.SetFrameRate(25, 256, 25);
		ASSERT_GE(enc.SetSize(176, 144), 0) << "ouverture de l'encodeur H264";

		VideoAccelStats open = VideoAccel::GetStats();
		EXPECT_EQ(before.encoders + 1, open.encoders);
		// L'encodeur dit lui-même s'il est matériel : le compteur doit le suivre,
		// que la machine ait un GPU ou non.
		EXPECT_EQ(before.encodersHw + (enc.IsHardwareReady() ? 1 : 0), open.encodersHw);
	}

	VideoAccelStats after = VideoAccel::GetStats();
	EXPECT_EQ(before.encoders,   after.encoders)   << "encodeur detruit toujours compte";
	EXPECT_EQ(before.encodersHw, after.encodersHw);
}

TEST(VideoAccel, DecoderIsCountedWhileOpen)
{
	VideoAccelStats before = VideoAccel::GetStats();

	{
		H264Decoder dec;
		VideoAccelStats open = VideoAccel::GetStats();
		EXPECT_EQ(before.decoders + 1, open.decoders);
		EXPECT_EQ(before.decodersHw + (dec.IsHardwareReady() ? 1 : 0), open.decodersHw);
	}

	VideoAccelStats after = VideoAccel::GetStats();
	EXPECT_EQ(before.decoders,   after.decoders) << "decodeur detruit toujours compte";
	EXPECT_EQ(before.decodersHw, after.decodersHw);
}

// Une réouverture à chaud (changement de taille) passe par CloseCodec puis
// OpenCodec : sans la symétrie des deux, le compteur dériverait d'une unité à
// chaque renégociation, et finirait par annoncer des encodeurs qui n'existent
// plus.
TEST(VideoAccel, ResizeDoesNotLeakCounters)
{
	Properties props;
	H264Encoder enc(props);
	enc.SetFrameRate(25, 256, 25);
	ASSERT_GE(enc.SetSize(176, 144), 0);

	VideoAccelStats opened = VideoAccel::GetStats();

	for (int i = 0; i < 5; i++)
	{
		ASSERT_GE(enc.SetSize(352, 288), 0);
		ASSERT_GE(enc.SetSize(176, 144), 0);
	}

	VideoAccelStats after = VideoAccel::GetStats();
	EXPECT_EQ(opened.encoders,   after.encoders);
	EXPECT_EQ(opened.encodersHw, after.encodersHw);
}

// ===========================================================================
// Compteur de replis matériel -> logiciel
// ===========================================================================

// Aucun GPU n'encode le H263 : demander le matériel pour ce codec force donc un
// repli logiciel, sur toute machine. C'est le seul repli qu'on sait provoquer
// sans dépendre du modèle de carte.
TEST(VideoAccel, FallbackIsCountedOnlyWhenHardwareWasThere)
{
	const bool hasDevice = Pict::GetVAAPIDevice() != nullptr;
	VideoAccelStats before = VideoAccel::GetStats();

	{
		Properties props;
		FfVideoEncoder enc(props, AV_CODEC_ID_H263, VideoCodec::H263_1998, /*tryHW*/ true);
		EXPECT_FALSE(enc.IsHardwareReady()) << "aucun encodeur VAAPI n'existe pour H263";
	}

	VideoAccelStats after = VideoAccel::GetStats();
	if (hasDevice)
		EXPECT_EQ(before.hwFallbacks + 1, after.hwFallbacks)
			<< "un repli subi alors que le GPU etait la doit se voir";
	else
		// Sans device, tout est logiciel : compter chaque codec comme un repli
		// noierait le signal (cf. VideoAccel::OnHwFallback).
		EXPECT_EQ(before.hwFallbacks, after.hwFallbacks)
			<< "sans GPU, le logiciel est la normale, pas un repli";
}

// Un encodeur logiciel ordinaire — personne n'a demandé le GPU — n'est pas un
// repli. Sans cette borne, le compteur monterait sur des chemins qui n'ont
// jamais visé le matériel, et ne voudrait plus rien dire.
TEST(VideoAccel, SoftwareOnlyEncoderIsNotAFallback)
{
	VideoAccelStats before = VideoAccel::GetStats();

	{
		Properties props;
		FfVideoEncoder enc(props, AV_CODEC_ID_H263, VideoCodec::H263_1998, /*tryHW*/ false);
	}

	EXPECT_EQ(before.hwFallbacks, VideoAccel::GetStats().hwFallbacks);
}

// ===========================================================================
// Le commutateur d'extinction
// ===========================================================================

TEST(VideoAccel, DisableVaapiSilencesTheDevice)
{
	// Sous-processus : l'extinction est définitive, et les tests suivants de
	// cette suite doivent retrouver leur GPU.
	EXPECT_EXIT(
		{
			Pict::DisableVAAPI();
			exit(Pict::GetVAAPIDevice() == nullptr ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}

// Après extinction, un encodeur qui aurait pris le GPU n'en prend plus : c'est
// ce qui rend la mesure « même charge, sans accélération » possible.
TEST(VideoAccel, DisableVaapiForcesEncodersOnCpu)
{
	EXPECT_EXIT(
		{
			Pict::DisableVAAPI();

			Properties props;
			H264Encoder enc(props);
			enc.SetFrameRate(25, 256, 25);
			int ok = enc.SetSize(176, 144);

			// L'encodeur doit s'ouvrir — en logiciel — et se déclarer non matériel.
			exit((ok >= 0 && !enc.IsHardwareReady()) ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}
