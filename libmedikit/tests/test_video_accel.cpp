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
#include <av1/av1codec.h>
#include <vector>
extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace {

const int DW = 320, DH = 240;

// Annex-B d'un flux H.264 encodé par libx264 en direct (libavcodec) : c'est le
// seul moyen de produire un profil que H264Encoder ne sort pas (High 4:4:4).
std::vector<std::vector<BYTE>> EncodeX264(AVPixelFormat fmt, int frames)
{
	std::vector<std::vector<BYTE>> out;
	const AVCodec* c = avcodec_find_encoder_by_name("libx264");
	if (!c)
		return out;
	AVCodecContext* e = avcodec_alloc_context3(c);
	e->width = DW; e->height = DH; e->pix_fmt = fmt;
	e->time_base = { 1, 25 }; e->gop_size = 25; e->max_b_frames = 0;
	av_opt_set(e->priv_data, "preset", "ultrafast", 0);
	av_opt_set(e->priv_data, "tune", "zerolatency", 0);
	AVFrame* f = av_frame_alloc();
	AVPacket* pkt = av_packet_alloc();
	if (avcodec_open2(e, c, nullptr) >= 0)
	{
		f->format = fmt; f->width = DW; f->height = DH;
		av_frame_get_buffer(f, 0);
		for (int i = 0; i <= frames; i++)
		{
			if (i < frames)
			{
				av_frame_make_writable(f);
				for (int p = 0; p < 3; p++)
				{
					int rows = p ? AV_CEIL_RSHIFT(DH, av_pix_fmt_desc_get(fmt)->log2_chroma_h) : DH;
					for (int y = 0; y < rows; y++)
						memset(f->data[p] + y * f->linesize[p], p ? 128 : 40 + i * 8, f->linesize[p]);
				}
				f->pts = i;
			}
			avcodec_send_frame(e, i < frames ? f : nullptr);
			while (avcodec_receive_packet(e, pkt) >= 0)
			{
				out.emplace_back(pkt->data, pkt->data + pkt->size);
				av_packet_unref(pkt);
			}
		}
	}
	av_packet_free(&pkt);
	av_frame_free(&f);
	avcodec_free_context(&e);
	return out;
}

// Décode chaque paquet et rend la dernière image sortie.
PictPtr DecodeAll(VideoDecoder& dec, std::vector<std::vector<BYTE>> packets)
{
	PictPtr last;
	for (std::vector<BYTE>& p : packets)
	{
		size_t len = p.size();
		p.resize(len + AV_INPUT_BUFFER_PADDING_SIZE, 0);
		PictPtr before = dec.GetFrame();
		dec.Decode(p.data(), len);
		if (dec.GetFrame() != before)
			last = dec.GetFrame();
	}
	return last;
}

} // namespace

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
		// Un device attaché ne prouve rien : le décodeur n'est matériel qu'une
		// fois sa première surface rendue.
		EXPECT_EQ(before.decodersHw, open.decodersHw);
	}

	VideoAccelStats after = VideoAccel::GetStats();
	EXPECT_EQ(before.decoders,   after.decoders) << "decodeur detruit toujours compte";
	EXPECT_EQ(before.decodersHw, after.decodersHw);
}

// Matériel = la dernière image rendue est une surface GPU. Avec ou sans GPU, le
// compteur suit ce que le décodeur a réellement rendu.
TEST(VideoAccel, DecoderIsHardwareOnlyOnceItOutputsASurface)
{
	std::vector<std::vector<BYTE>> stream = EncodeX264(AV_PIX_FMT_YUV420P, 5);
	if (stream.empty())
		GTEST_SKIP() << "libx264 absent du ffmpeg installe";
	VideoAccelStats before = VideoAccel::GetStats();
	{
		H264Decoder dec;
		PictPtr last = DecodeAll(dec, stream);
		ASSERT_TRUE(last != nullptr);
		VideoAccelStats open = VideoAccel::GetStats();
		EXPECT_EQ(before.decodersHw + (last->IsGPUPict() ? 1 : 0), open.decodersHw);
		EXPECT_EQ(before.hwFallbacks, open.hwFallbacks) << "un flux 4:2:0 ordinaire n'est pas un repli";
	}
	VideoAccelStats after = VideoAccel::GetStats();
	EXPECT_EQ(before.decoders,   after.decoders);
	EXPECT_EQ(before.decodersHw, after.decodersHw);
}

// Un codec sans chemin VAAPI (libdav1d) ne visait pas le matériel : ce n'est pas
// un repli, même sur un poste équipé. Il en comptait un à chaque ouverture.
TEST(VideoAccel, DecoderWithoutVaapiPathIsNotAFallback)
{
	VideoAccelStats before = VideoAccel::GetStats();
	{
		AV1Decoder dec;
	}
	EXPECT_EQ(before.hwFallbacks, VideoAccel::GetStats().hwFallbacks);
}

// Le cas qui motive la redéfinition : device attaché, flux que le driver ne
// décode pas. libavcodec passe en logiciel sans le dire ; le compteur, lui,
// doit le dire — une fois, pas une fois par image.
TEST(VideoAccel, DISABLED_ProfileTheDriverCannotDecodeCountsOneFallback)
{
	ASSERT_NE(Pict::GetVAAPIDevice(), nullptr) << "pas de device VAAPI";
	std::vector<std::vector<BYTE>> stream = EncodeX264(AV_PIX_FMT_YUV444P, 10);
	ASSERT_GE(stream.size(), 8u) << "libx264 4:4:4 indisponible";
	VideoAccelStats before = VideoAccel::GetStats();
	{
		H264Decoder dec;
		ASSERT_TRUE(dec.IsHardwareReady());
		PictPtr last = DecodeAll(dec, stream);
		ASSERT_TRUE(last != nullptr);
		EXPECT_FALSE(last->IsGPUPict()) << "le driver decode le 4:4:4 : ce test ne prouve rien ici";
		VideoAccelStats open = VideoAccel::GetStats();
		EXPECT_EQ(before.decodersHw, open.decodersHw) << "decodeur logiciel compte comme GPU";
		EXPECT_EQ(before.hwFallbacks + 1, open.hwFallbacks);
	}
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
