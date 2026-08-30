/**
 * test_audio_codecs.cpp — transport des échantillons décompressés en AVFrame
 * refcompté (Samples/SamplesPtr) et contrat des codecs audio.
 *
 * Cette suite couvre la classe de bug qui a motivé la migration (appel
 * opus<->speex16 du 2026-08-14) : tampons appelants
 * trop courts, trames tronquées, fréquence périmée entre deux bouts d'un pipe.
 * Le premier défaut n'a plus de test propre : il n'existe plus de tampon
 * appelant à déborder, l'interface n'en demande aucun.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/audio.h>
#include <medkit/codecs.h>
#include <vector>
#include <cmath>
#include <memory>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {

// Sinusoïde S16 mono : un signal non nul et non constant, pour que les codecs
// à silence/DTX ne puissent pas produire de trames vides sans qu'on le voie.
std::vector<SWORD> MakeTone(int nb, int rate, double freq = 440.0)
{
	std::vector<SWORD> pcm(nb);
	for (int i = 0; i < nb; i++)
		pcm[i] = (SWORD)(8000.0 * sin(2.0 * M_PI * freq * i / rate));
	return pcm;
}

SamplesPtr MakeToneSamples(int nb, int rate)
{
	std::vector<SWORD> pcm = MakeTone(nb, rate);
	return Samples::FromBuffer(&pcm[0], (DWORD)nb, (DWORD)rate);
}

// Encodeur/décodeur du couple demandé, ou nullptr si ffmpeg ne l'a pas.
std::unique_ptr<AudioEncoder> MakeEncoder(AudioCodec::Type type)
{
	Properties props;
	return std::unique_ptr<AudioEncoder>(AudioCodecFactory::CreateEncoder(type, props));
}

std::unique_ptr<AudioDecoder> MakeDecoder(AudioCodec::Type type)
{
	return std::unique_ptr<AudioDecoder>(AudioCodecFactory::CreateDecoder(type));
}

} // namespace

/* ------------------------------------------------------------------------- *
 *                              Samples : invariant                          *
 * ------------------------------------------------------------------------- */

TEST(AudioSamples, AllocPorteSesMetadonnees)
{
	SamplesPtr s = Samples::Alloc(960, 48000);
	ASSERT_TRUE(s != nullptr);
	EXPECT_EQ(s->GetNbSamples(), 960u);
	EXPECT_EQ(s->GetRate(), 48000u);
	ASSERT_TRUE(s->GetData() != nullptr);
	EXPECT_TRUE(Samples::IsS16Mono(s->GetAVFrame()));
}

TEST(AudioSamples, AllocRefuseUneTailleOuUneFrequenceNulle)
{
	EXPECT_TRUE(Samples::Alloc(0, 48000) == nullptr);
	EXPECT_TRUE(Samples::Alloc(960, 0) == nullptr);
}

TEST(AudioSamples, FromBufferRecopieLesEchantillons)
{
	std::vector<SWORD> pcm = MakeTone(160, 8000);
	SamplesPtr s = Samples::FromBuffer(&pcm[0], 160, 8000);
	ASSERT_TRUE(s != nullptr);
	EXPECT_EQ(s->GetNbSamples(), 160u);
	EXPECT_EQ(0, memcmp(s->GetData(), &pcm[0], 160 * sizeof(SWORD)));
}

// Adverse : l'invariant de transport (S16 mono) est REFUSÉ à la porte, pas
// découvert plus loin par un GetData() qui lirait un plan planar comme du S16.
TEST(AudioSamples, RefuseCeQuiNEstPasS16Mono)
{
	AVFrame *stereo = av_frame_alloc();
	ASSERT_TRUE(stereo != nullptr);
	stereo->format      = AV_SAMPLE_FMT_S16;
	stereo->sample_rate = 48000;
	stereo->nb_samples  = 960;
	av_channel_layout_default(&stereo->ch_layout, 2);
	ASSERT_GE(av_frame_get_buffer(stereo, 0), 0);
	EXPECT_TRUE(Samples::FromAVFrame(stereo) == nullptr);
	av_frame_free(&stereo);

	AVFrame *planar = av_frame_alloc();
	ASSERT_TRUE(planar != nullptr);
	planar->format      = AV_SAMPLE_FMT_FLTP;
	planar->sample_rate = 48000;
	planar->nb_samples  = 960;
	av_channel_layout_default(&planar->ch_layout, 1);
	ASSERT_GE(av_frame_get_buffer(planar, 0), 0);
	EXPECT_TRUE(Samples::FromAVFrame(planar) == nullptr);
	av_frame_free(&planar);

	EXPECT_TRUE(Samples::FromAVFrame(nullptr) == nullptr);
}

/* ------------------------------------------------------------------------- *
 *                          G.711 (désormais via ffmpeg)                     *
 * ------------------------------------------------------------------------- */

TEST(AudioCodecG711, LaTrancheAnnonceeEstDe20ms)
{
	// Le mcu dimensionne ses lectures sur numFrameSamples : les encodeurs PCM
	// de ffmpeg n'annoncent aucun frame_size, c'est Open() qui impose 20 ms.
	std::unique_ptr<AudioEncoder> u = MakeEncoder(AudioCodec::PCMU);
	std::unique_ptr<AudioEncoder> a = MakeEncoder(AudioCodec::PCMA);
	ASSERT_TRUE(u != nullptr);
	ASSERT_TRUE(a != nullptr);
	EXPECT_EQ(u->numFrameSamples, 160);
	EXPECT_EQ(a->numFrameSamples, 160);
	EXPECT_EQ(u->GetRate(), 8000u);
	EXPECT_EQ(u->GetClockRate(), 8000u);
}

TEST(AudioCodecG711, AllerRetourPcmu)
{
	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::PCMU);
	std::unique_ptr<AudioDecoder> dec = MakeDecoder(AudioCodec::PCMU);
	ASSERT_TRUE(enc != nullptr);
	ASSERT_TRUE(dec != nullptr);

	SamplesPtr in = MakeToneSamples(160, 8000);
	ASSERT_TRUE(in != nullptr);

	AudioFramePtr frame = enc->EncodeFrame(in);
	ASSERT_TRUE(frame != nullptr);
	EXPECT_EQ(frame->GetLength(), 160u);	// 1 octet par échantillon

	ASSERT_GT(dec->Decode(frame->GetData(), (int)frame->GetLength()), 0);
	SamplesPtr out = dec->GetFrame();
	ASSERT_TRUE(out != nullptr);
	EXPECT_EQ(out->GetNbSamples(), 160u);
	EXPECT_EQ(out->GetRate(), 8000u);
	EXPECT_TRUE(dec->GetFrame() == nullptr);	// une seule trame

	// G.711 est fortement quantifié : on vérifie la forme, pas l'exactitude.
	double err = 0;
	for (DWORD i = 0; i < 160; i++)
		err += fabs((double)out->GetData()[i] - (double)in->GetData()[i]);
	EXPECT_LT(err / 160.0, 200.0);
}

TEST(AudioCodecG711, AllerRetourPcma)
{
	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::PCMA);
	std::unique_ptr<AudioDecoder> dec = MakeDecoder(AudioCodec::PCMA);
	ASSERT_TRUE(enc != nullptr);
	ASSERT_TRUE(dec != nullptr);

	AudioFramePtr frame = enc->EncodeFrame(MakeToneSamples(160, 8000));
	ASSERT_TRUE(frame != nullptr);
	EXPECT_EQ(frame->GetLength(), 160u);

	ASSERT_GT(dec->Decode(frame->GetData(), (int)frame->GetLength()), 0);
	SamplesPtr out = dec->GetFrame();
	ASSERT_TRUE(out != nullptr);
	EXPECT_EQ(out->GetNbSamples(), 160u);
}

/* ------------------------------------------------------------------------- *
 *      L'encodeur accepte n'importe quelle taille (av_audio_fifo interne)    *
 * ------------------------------------------------------------------------- */

TEST(AudioCodecFifo, LEncodeurAccepteDesTaillesQuelconques)
{
	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::PCMU);
	ASSERT_TRUE(enc != nullptr);

	// Rien à 100 échantillons : la fifo retient, elle n'invente pas de trame.
	EXPECT_TRUE(enc->EncodeFrame(MakeToneSamples(100, 8000)) == nullptr);

	// 100 + 100 = 200 >= 160 : une trame sort, 40 échantillons restent.
	AudioFramePtr frame = enc->EncodeFrame(MakeToneSamples(100, 8000));
	ASSERT_TRUE(frame != nullptr);
	EXPECT_EQ(frame->GetLength(), 160u);
	EXPECT_TRUE(enc->EncodeFrame(nullptr) == nullptr);

	// Une trame plus grosse qu'une tranche produit plusieurs trames, retirées
	// par purges successives : rien n'est perdu, rien n'est concaténé.
	int emitted = 0;
	for (AudioFramePtr f = enc->EncodeFrame(MakeToneSamples(1000, 8000));
	     f; f = enc->EncodeFrame(nullptr))
	{
		EXPECT_EQ(f->GetLength(), 160u);
		emitted++;
	}
	// 40 en attente + 1000 = 1040 -> 6 tranches de 160, reste 80.
	EXPECT_EQ(emitted, 6);
}

/* ------------------------------------------------------------------------- *
 *         Bug n°2 du 14/08 : plus aucune trame tronquée au décodage         *
 * ------------------------------------------------------------------------- */

TEST(AudioCodecOpus, LaTrameDecodeeSortEntiere)
{
	if (!AudioCodec::IsSupported(AudioCodec::OPUS))
		GTEST_SKIP() << "OPUS indisponible dans ffmpeg";

	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::OPUS);
	std::unique_ptr<AudioDecoder> dec = MakeDecoder(AudioCodec::OPUS);
	ASSERT_TRUE(enc != nullptr);
	ASSERT_TRUE(dec != nullptr);
	ASSERT_EQ(enc->TrySetRate(48000), 48000u);
	ASSERT_EQ(enc->numFrameSamples, 960);	// 20 ms à 48 kHz

	// C'est le cas exact du 14/08 : 960 échantillons par trame face aux
	// tampons appelants de 512. La trame doit sortir ENTIÈRE, en un morceau.
	AudioFramePtr frame = enc->EncodeFrame(MakeToneSamples(960, 48000));
	ASSERT_TRUE(frame != nullptr);
	ASSERT_GT(frame->GetLength(), 0u);

	ASSERT_GT(dec->Decode(frame->GetData(), (int)frame->GetLength()), 0);
	SamplesPtr out = dec->GetFrame();
	ASSERT_TRUE(out != nullptr);
	EXPECT_EQ(out->GetNbSamples(), 960u);
	EXPECT_EQ(out->GetRate(), 48000u);
	EXPECT_TRUE(dec->GetFrame() == nullptr);
}

TEST(AudioCodecOpus, DixTramesEncodeesDonnentDixTramesDecodees)
{
	if (!AudioCodec::IsSupported(AudioCodec::OPUS))
		GTEST_SKIP() << "OPUS indisponible dans ffmpeg";

	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::OPUS);
	std::unique_ptr<AudioDecoder> dec = MakeDecoder(AudioCodec::OPUS);
	ASSERT_TRUE(enc != nullptr);
	ASSERT_TRUE(dec != nullptr);
	ASSERT_EQ(enc->TrySetRate(48000), 48000u);

	// Le débit doit se conserver de bout en bout : c'est le symptôme mesuré le
	// 14/08 (53 % du débit, 25 paquets/s au lieu de 50).
	DWORD total = 0;
	int frames = 0;
	for (int i = 0; i < 10; i++)
	{
		AudioFramePtr f = enc->EncodeFrame(MakeToneSamples(960, 48000));
		if (!f)
			continue;
		ASSERT_GT(dec->Decode(f->GetData(), (int)f->GetLength()), 0);
		for (SamplesPtr s = dec->GetFrame(); s; s = dec->GetFrame())
		{
			total += s->GetNbSamples();
			frames++;
		}
	}
	EXPECT_EQ(frames, 10);
	EXPECT_EQ(total, 9600u);
}

/* ------------------------------------------------------------------------- *
 *   Bug n°3 du 14/08 : la trame fait foi, la fréquence n'est plus partagée   *
 * ------------------------------------------------------------------------- */

TEST(AudioCodecResample, UnChangementDeFrequenceEstAbsorbeParLaTrame)
{
	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::PCMU);
	ASSERT_TRUE(enc != nullptr);
	ASSERT_EQ(enc->TrySetRate(8000), 8000u);

	// Le producteur change de fréquence en cours de route (le décodeur qui
	// découvre son vrai 16 kHz après le premier paquet). Personne ne prévient
	// l'encodeur : la trame le dit, il se reconfigure seul.
	//
	// Le rééchantillonneur a une latence d'amorçage : la toute première
	// conversion rend un peu moins de 160 échantillons. On mesure donc le
	// DÉBIT sur dix trames — c'est lui que le bug du 14/08 dégradait.
	int emitted = 0;
	for (int i = 0; i < 10; i++)
		for (AudioFramePtr f = enc->EncodeFrame(MakeToneSamples(320, 16000));
		     f; f = enc->EncodeFrame(nullptr))
		{
			EXPECT_EQ(f->GetLength(), 160u);	// 20 ms à 8 kHz
			emitted++;
		}
	// 3200 échantillons à 16 kHz = 1600 à 8 kHz = 10 tranches, à l'amorçage près.
	EXPECT_GE(emitted, 9);
	EXPECT_LE(emitted, 10);

	// Retour à 8 kHz : le resampler est vidé avant d'être défait, ce qu'il
	// retenait n'est pas perdu, et l'encodeur repart sans état résiduel.
	AudioFramePtr frame = enc->EncodeFrame(MakeToneSamples(160, 8000));
	ASSERT_TRUE(frame != nullptr);
	EXPECT_EQ(frame->GetLength(), 160u);
}

/* ------------------------------------------------------------------------- *
 *                                  Adverses                                 *
 * ------------------------------------------------------------------------- */

TEST(AudioCodecAdverse, UnPaquetInvalideNeProduitAucuneTrame)
{
	std::unique_ptr<AudioDecoder> dec = MakeDecoder(AudioCodec::OPUS);
	if (!dec)
		GTEST_SKIP() << "OPUS indisponible dans ffmpeg";

	BYTE garbage[64];
	memset(garbage, 0xFF, sizeof(garbage));
	dec->Decode(garbage, sizeof(garbage));	// ne doit ni planter ni fabriquer
	EXPECT_TRUE(dec->GetFrame() == nullptr);

	EXPECT_EQ(dec->Decode(NULL, 0), 0);
	EXPECT_EQ(dec->Decode(garbage, 0), 0);
	EXPECT_TRUE(dec->GetFrame() == nullptr);
}

TEST(AudioCodecAdverse, EncoderUneTrameVideOuNulleNeProduitRien)
{
	std::unique_ptr<AudioEncoder> enc = MakeEncoder(AudioCodec::PCMU);
	ASSERT_TRUE(enc != nullptr);

	EXPECT_TRUE(enc->EncodeFrame(nullptr) == nullptr);
	EXPECT_TRUE(enc->EncodeFrame(SamplesPtr()) == nullptr);
	EXPECT_TRUE(enc->EncodeFrame(std::make_shared<Samples>()) == nullptr);
}
