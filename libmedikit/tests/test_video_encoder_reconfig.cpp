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
 * Les trames de bruit sont peu compressibles : le débit de sortie colle à la
 * consigne interne de l'encodeur, ce qui rend la consigne observable. Le bruit
 * est d'amplitude LIMITÉE (±32) : du bruit pur sur les trois plans a un
 * plancher — 16 Ko par image 320x240 au quantificateur maximal en VP8 temps
 * réel — sous lequel aucune consigne ne peut descendre, et le test mesurait
 * alors ce plancher, pas la consigne (constaté le 2026-08-29 en passant libvpx
 * en `deadline=realtime`).
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <vp8/vp8encoder.h>
#include <av1/av1codec.h>

namespace {

const int W = 320, H = 240;

// Bruit déterministe (LCG) d'amplitude ±32 autour du gris : chaque trame est
// différente, coûteuse à coder, mais compressible jusqu'aux consignes testées.
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
		VideoFramePtr vf = enc.EncodeFrame(pic);
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

	VideoFramePtr vf;
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

// La boucle d'adaptation monte de +8 %/s. Avec un seuil de réouverture à 10 %
// elle franchit le seuil toutes les 1,3 s, soit une trame clé toutes les 1,3 s :
// mesuré en séance le 2026-08-20, 136 réouvertures en 5,5 min dont deux à 0,16 s
// d'intervalle. Les paliers de 1,5x bornent cela à la hausse.
TEST(VideoEncoderReconfig, UneRampeDeMonteeNeMultipliePasLesTramesClesVp8)
{
	DWORD seed = 45;
	VP8Encoder enc((Properties()));
	int kbits = 300;
	ASSERT_EQ(enc.SetFrameRate(30, kbits, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	// 30 pas de +8 % : x10 au total, soit 5 paliers de 1,5x franchis.
	size_t intras = 0, frames = 0;
	for (int i = 0; i < 30; i++)
	{
		kbits = kbits + kbits * 8 / 100;
		ASSERT_EQ(enc.SetFrameRate(30, kbits, 300), 1);
		PictPtr pic = CreateNoise(seed);
		ASSERT_TRUE(pic != nullptr);
		VideoFramePtr vf = enc.EncodeFrame(pic);
		if (!vf)
			continue;
		frames++;
		if (vf->IsIntra())
			intras++;
	}
	ASSERT_GE(frames, 20u);
	// 5 paliers franchis, plus la trame clé d'ouverture : 8 laisse de la marge.
	// Le seuil symétrique de 10 % en produisait plus de 20.
	EXPECT_LE(intras, 8u) << intras << " trames clés pour une rampe x10";
	// Et la consigne a bien fini par être appliquée : le débit ouvert doit
	// avoir suivi, sinon aucune trame clé n'aurait été produite du tout.
	EXPECT_GE(intras, 1u);
}

// La baisse, elle, doit passer tout de suite : c'est le sens qui compte quand le
// réseau se ferme, et un pas de l'AIMD ne vaut que -15 %.
TEST(VideoEncoderReconfig, UnPasDeBaisseEstAppliqueImmediatementVp8)
{
	DWORD seed = 46;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	EncodeRun warmup = Encode(enc, 5, seed);
	ASSERT_GE(warmup.frames, 1u);

	// -15 %, un seul pas de l'AIMD (0,85 x l'acquitté)
	ASSERT_EQ(enc.SetFrameRate(30, 1700, 300), 1);

	EncodeRun after = Encode(enc, 5, seed);
	ASSERT_GE(after.frames, 1u);
	EXPECT_TRUE(after.firstIsIntra) << "la baisse n'a pas été appliquée";
}

// ── Cadence : §3.6 de jsr309_transcode_sans_thread.md ────────────────────────
// `fps` n'est lu qu'à OpenCodec (time_base, rc_buffer_size, gop_size) et aucun
// wrapper ffmpeg ne le reconfigure à chaud. Sans réouverture, une source à
// 15 im/s dans un encodeur ouvert à 30 reçoit un budget de bitrate/30 par image
// et sort à la moitié du débit négocié.
TEST(VideoEncoderReconfig, UnChangementDeCadenceRouvreVp8)
{
	DWORD seed = 47;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	// Passe la trame clé d'ouverture.
	EncodeRun warmup = Encode(enc, 5, seed);
	ASSERT_GE(warmup.frames, 1u);

	// Cadence divisée par deux, débit INCHANGÉ : seule la cadence peut
	// déclencher la réouverture.
	ASSERT_EQ(enc.SetFrameRate(15, 2000, 150), 1);

	EncodeRun after = Encode(enc, 5, seed);
	ASSERT_GE(after.frames, 1u);
	EXPECT_TRUE(after.firstIsIntra)
		<< "la cadence n'a pas ete appliquee : codec non rouvert";
}

// L'hystérésis du mcu ne laisse passer que des écarts de plus de 25 %, mais la
// politique doit tenir seule : une variation de quelques pour cent ne coûte pas
// une trame clé.
TEST(VideoEncoderReconfig, UnePetiteVariationDeCadenceNeRouvrePasVp8)
{
	DWORD seed = 48;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	EncodeRun warmup = Encode(enc, 5, seed);
	ASSERT_GE(warmup.frames, 1u);

	ASSERT_EQ(enc.SetFrameRate(28, 2000, 300), 1);	// -6,7 %

	EncodeRun after = Encode(enc, 10, seed);
	ASSERT_GE(after.frames, 5u);
	EXPECT_EQ(after.intras, 0u) << "reouverture sur un ecart de cadence de 7 %";
}

// Même contrat pour AV1, dont le rate control se règle sur `frame_rate`.
TEST(VideoEncoderReconfig, UnChangementDeCadenceRouvreAv1)
{
	DWORD seed = 49;
	Properties props;
	props.SetProperty("av1.preset", "12");
	AV1Encoder enc(props);
	ASSERT_EQ(enc.SetFrameRate(30, 2000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	VideoFramePtr vf;
	for (int i = 0; i < 100 && !vf; i++)
	{
		PictPtr pic = CreateNoise(seed);
		ASSERT_TRUE(pic != nullptr);
		vf = enc.EncodeFrame(pic);
	}
	ASSERT_TRUE(vf != nullptr) << "SVT-AV1 n'a rien rendu en 100 trames";

	// Débit inchangé, cadence divisée par deux.
	ASSERT_EQ(enc.SetFrameRate(15, 2000, 150), 1);

	vf = nullptr;
	for (int i = 0; i < 100 && !vf; i++)
	{
		PictPtr pic = CreateNoise(seed);
		ASSERT_TRUE(pic != nullptr);
		vf = enc.EncodeFrame(pic);
	}
	ASSERT_TRUE(vf != nullptr) << "SVT-AV1 n'a rien rendu apres le changement de cadence";
	EXPECT_TRUE(vf->IsIntra())
		<< "pas de trame cle apres le changement de cadence : codec non rouvert";
}
