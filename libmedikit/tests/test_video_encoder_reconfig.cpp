/**
 * test_video_encoder_reconfig.cpp — la consigne de débit posée APRÈS
 * l'ouverture doit atteindre l'encodeur.
 *
 * FfVideoEncoder::SetFrameRate ne fait que mémoriser fps/débit/intra : seuls
 * les codecs qui le surchargent appliquent la consigne à chaud. H264 le
 * faisait ; VP8 et AV1 non — l'encodeur gardait son débit d'ouverture pour
 * toute sa vie (mesuré en appel réel le 2026-08-20 : VP8 figé à 10 240 kb/s,
 * le ×5 « première image » de videostream jamais restauré, toute la boucle
 * d'adaptation inopérante).
 *
 * Les trames de bruit sont incompressibles : le débit de sortie colle à la
 * consigne interne de l'encodeur, ce qui rend la consigne observable.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <vp8/vp8encoder.h>
#include <av1/av1codec.h>

namespace {

const int W = 320, H = 240;

// Bruit déterministe (LCG) : chaque trame est différente et incompressible.
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
				line[x] = (seed >> 16) & 0xFF;
			}
		}
	}
	return pic;
}

struct EncodeRun
{
	size_t frames;		// trames rendues par l'encodeur
	size_t intras;		// dont trames clés
	bool   firstIsIntra;	// la première rendue est une trame clé
	double avgInterBytes;	// taille moyenne des trames NON clés
};

// Encode `count` trames de bruit et mesure ce qui sort. Un encodeur peut
// bufferiser (SVT-AV1) : on ne compte que les trames effectivement rendues.
EncodeRun Encode(VideoEncoder& enc, int count, DWORD& seed)
{
	EncodeRun run = { 0, 0, false, 0.0 };
	double interBytes = 0;
	size_t inters = 0;
	for (int i = 0; i < count; i++)
	{
		PictPtr pic = CreateNoise(seed);
		if (!pic)
			return run;
		VideoFrame* vf = enc.EncodeFrame(pic);
		if (!vf)
			continue;
		if (!run.frames)
			run.firstIsIntra = vf->IsIntra();
		run.frames++;
		if (vf->IsIntra())
		{
			run.intras++;
		}
		else
		{
			interBytes += vf->GetLength();
			inters++;
		}
	}
	if (inters)
		run.avgInterBytes = interBytes / inters;
	return run;
}

} // namespace

// Sans surcharge de SetFrameRate, la consigne posée après l'ouverture est
// perdue : le débit de sortie reste celui de l'ouverture (test ROUGE avant
// le correctif, VERT avec).
TEST(VideoEncoderReconfig, LaConsigneAtteintLEncodeurVp8)
{
	DWORD seed = 42;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	EncodeRun before = Encode(enc, 20, seed);
	ASSERT_GE(before.frames, 10u);
	ASSERT_GT(before.avgInterBytes, 0.0);

	// Consigne divisée par 10 : la boucle d'adaptation vient de détecter une
	// congestion, l'encodeur doit suivre.
	ASSERT_EQ(enc.SetFrameRate(30, 200, 300), 1);

	EncodeRun after = Encode(enc, 20, seed);
	ASSERT_GE(after.frames, 10u);
	ASSERT_GT(after.avgInterBytes, 0.0);

	// La réouverture produit une trame clé au nouveau débit.
	EXPECT_TRUE(after.firstIsIntra);
	// Sur du bruit, ÷10 de consigne doit se voir largement (marge : ÷3).
	EXPECT_LT(after.avgInterBytes * 3, before.avgInterBytes)
		<< "avant=" << before.avgInterBytes << " octets/trame, après="
		<< after.avgInterBytes << " : la consigne n'a pas été appliquée";
}

// Un micro-ajustement (<10 %) ne doit PAS rouvrir le codec : pas de trame clé
// à chaque tic de la boucle d'adaptation (+8 %/s de videostream).
TEST(VideoEncoderReconfig, UnMicroAjustementNeRouvrePasVp8)
{
	DWORD seed = 43;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	// Passe la trame clé d'ouverture.
	EncodeRun warmup = Encode(enc, 5, seed);
	ASSERT_GE(warmup.frames, 1u);

	ASSERT_EQ(enc.SetFrameRate(30, 1900, 300), 1);	// -5 %

	EncodeRun after = Encode(enc, 10, seed);
	ASSERT_GE(after.frames, 5u);
	EXPECT_EQ(after.intras, 0u) << "réouverture (trame clé) sur un écart de 5 %";
}

// Même contrat pour AV1. SVT-AV1 bufferise (lookahead ~37 trames) : on nourrit
// jusqu'à la première trame rendue, et on ne juge que la trame clé de
// réouverture — c'est elle qui prouve que la consigne a été appliquée.
TEST(VideoEncoderReconfig, LaConsigneAtteintLEncodeurAv1)
{
	DWORD seed = 44;
	Properties props;
	// Preset le plus rapide : le test juge la reconfiguration, pas la qualité.
	props.SetProperty("av1.preset", "12");
	AV1Encoder enc(props);
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	VideoFrame* vf = nullptr;
	for (int i = 0; i < 100 && !vf; i++)
	{
		PictPtr pic = CreateNoise(seed);
		ASSERT_TRUE(pic != nullptr);
		vf = enc.EncodeFrame(pic);
	}
	ASSERT_TRUE(vf != nullptr) << "SVT-AV1 n'a rien rendu en 100 trames";

	ASSERT_EQ(enc.SetFrameRate(30, 200, 300), 1);

	vf = nullptr;
	for (int i = 0; i < 100 && !vf; i++)
	{
		PictPtr pic = CreateNoise(seed);
		ASSERT_TRUE(pic != nullptr);
		vf = enc.EncodeFrame(pic);
	}
	ASSERT_TRUE(vf != nullptr) << "SVT-AV1 n'a rien rendu après la reconfiguration";
	EXPECT_TRUE(vf->IsIntra())
		<< "pas de trame clé après le changement de consigne : codec non rouvert";
}
