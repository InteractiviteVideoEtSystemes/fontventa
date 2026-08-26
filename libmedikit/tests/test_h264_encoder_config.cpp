/**
 * test_h264_encoder_config.cpp — rate control de l'encodeur H264 logiciel :
 * CRF par budget (bits/pixel/image, avec hystérésis) et reconfiguration à
 * chaud par le wrapper libx264 (VBV et CRF relus à chaque trame, jamais de
 * réouverture ni de trame clé parasite).
 *
 * Les trames de bruit sont incompressibles : le débit de sortie colle au
 * plafond VBV de l'encodeur, ce qui rend rc_max_rate observable.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <h264/h264encoder.h>

namespace {

const int W = 320, H = 240;
const int FPS = 30;

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
	size_t frames;
	size_t intras;
	double avgBytes;	// taille moyenne des trames NON clés
};

// Encode `count` trames de bruit ; `skip` premières trames rendues exclues de
// la moyenne (drainage du VBV initial).
EncodeRun Encode(VideoEncoder& enc, int count, DWORD& seed, size_t skip = 0)
{
	EncodeRun run = { 0, 0, 0.0 };
	double bytes = 0;
	size_t counted = 0;
	for (int i = 0; i < count; i++)
	{
		PictPtr pic = CreateNoise(seed);
		if (!pic)
			return run;
		VideoFrame* vf = enc.EncodeFrame(pic);
		if (!vf)
			continue;
		run.frames++;
		if (vf->IsIntra())
		{
			run.intras++;
			continue;
		}
		if (run.frames > skip)
		{
			bytes += vf->GetLength();
			counted++;
		}
	}
	if (counted)
		run.avgBytes = bytes / counted;
	return run;
}

// Octets profil/contraintes/niveau du SPS d'une trame (NALs préfixées taille
// 4 octets, cf. H264Encoder::PacketizeFrame) ; vide si pas de SPS.
std::vector<BYTE> SpsProfileBytes(VideoFrame* vf)
{
	BYTE* data = vf->GetData();
	DWORD len  = vf->GetLength();
	DWORD pos  = 0;
	while (pos + 4 < len)
	{
		DWORD nalSize = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3];
		if (pos + 4 + nalSize > len)
			break;
		BYTE* nal = data + pos + 4;
		if (nalSize >= 4 && (nal[0] & 0x1f) == 7)
			return std::vector<BYTE>(nal + 1, nal + 4);
		pos += 4 + nalSize;
	}
	return std::vector<BYTE>();
}

// Encode jusqu'à obtenir une trame, rendue telle quelle (pour lire son SPS).
VideoFrame* EncodeOne(VideoEncoder& enc, DWORD& seed)
{
	for (int i = 0; i < 10; i++)
	{
		PictPtr pic = CreateNoise(seed);
		if (!pic)
			return nullptr;
		VideoFrame* vf = enc.EncodeFrame(pic);
		if (vf)
			return vf;
	}
	return nullptr;
}

} // namespace

// Les trois régimes de la fonction pure : large (>= 0,08 bpp), nominal, serré
// (<= 0,04 bpp).
TEST(H264CrfBudget, LesTroisRegimes)
{
	EXPECT_EQ(H264Encoder::CrfForBudget(0.10, 23), 21);
	EXPECT_EQ(H264Encoder::CrfForBudget(0.06, 23), 23);
	EXPECT_EQ(H264Encoder::CrfForBudget(0.03, 23), 26);
}

// Le seuil de SORTIE d'un régime est décalé de ~10 % : le même bpp garde le
// régime courant mais ne suffit pas à y entrer depuis le régime nominal.
TEST(H264CrfBudget, HysteresisAutourDesSeuils)
{
	EXPECT_EQ(H264Encoder::CrfForBudget(0.075, 21), 21);
	EXPECT_EQ(H264Encoder::CrfForBudget(0.075, 23), 23);
	EXPECT_EQ(H264Encoder::CrfForBudget(0.042, 26), 26);
	EXPECT_EQ(H264Encoder::CrfForBudget(0.042, 23), 23);
}

// Budget inconnu (taille ou cadence pas encore posées) : ne rien changer.
TEST(H264CrfBudget, BudgetInconnuNeChangeRien)
{
	EXPECT_EQ(H264Encoder::CrfForBudget(0.0, 23), 23);
	EXPECT_EQ(H264Encoder::CrfForBudget(-1.0, 21), 21);
}

// Sur du bruit le débit de sortie colle au plafond VBV : il doit valoir ~90 %
// de la consigne, pas les 60 % historiques qui ancraient bas la croyance de
// débit du pair TMMBR.
TEST(H264EncoderRc, LaCreteSuitLaConsigne)
{
	DWORD seed = 42;
	const int kbits = 1000;
	H264Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(FPS, kbits, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	// 30 trames, les 10 premières exclues (drainage du VBV initial).
	EncodeRun run = Encode(enc, 30, seed, 10);
	ASSERT_GE(run.frames, 25u);
	ASSERT_GT(run.avgBytes, 0.0);

	// Octets par trame si tout le budget passait : consigne / 8 / fps.
	const double budget = kbits * 1024.0 / 8 / FPS;
	EXPECT_GT(run.avgBytes, 0.72 * budget)
		<< run.avgBytes << " octets/trame : le plafond n'atteint pas ~90 % de "
		<< budget << " (l'ancien 60 % en donnait " << 0.6 * budget << ")";
	EXPECT_LT(run.avgBytes, 1.05 * budget)
		<< run.avgBytes << " octets/trame : le VBV ne borne plus la consigne";
}

// Une chute de consigne (AIMD, TMMBR) s'applique à chaud — y compris le
// changement de régime CRF qu'elle implique — sans réouverture, donc sans
// trame clé parasite.
TEST(H264EncoderRc, LaBaisseSAppliqueSansTrameCle)
{
	DWORD seed = 43;
	H264Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(FPS, 2000, 300), 1);	// 0,89 bpp -> CRF 21
	ASSERT_GE(enc.SetSize(W, H), 1);

	EncodeRun before = Encode(enc, 10, seed, 3);
	ASSERT_GE(before.frames, 8u);
	ASSERT_GT(before.avgBytes, 0.0);

	ASSERT_EQ(enc.SetFrameRate(FPS, 60, 300), 1);	// 0,027 bpp -> CRF 26

	EncodeRun after = Encode(enc, 30, seed, 5);
	ASSERT_GE(after.frames, 25u);
	ASSERT_GT(after.avgBytes, 0.0);

	EXPECT_EQ(after.intras, 0u) << "trame clé après la baisse : le codec a été rouvert";
	EXPECT_LT(after.avgBytes * 3, before.avgBytes)
		<< "avant=" << before.avgBytes << " octets/trame, après=" << after.avgBytes
		<< " : la consigne n'a pas été appliquée";
}

// Un profile-level-id malformé venu du contrôleur (le chemin /mcu recopie la
// map XML-RPC sans validation) ne doit pas jeter dans le thread d'encodage —
// "4d0" faisait un std::out_of_range dans GetProfileLevel (substr(4,2)), donc
// un terminate() du serveur entier. Repli attendu : 42801F, écrit dans le SPS.
TEST(H264EncoderPlid, UnPlidMalformeSeReplieSur42801F)
{
	const char* bad[] = { "4d0", "z", "", "42e01f7", "profil" };
	for (const char* plid : bad)
	{
		DWORD seed = 47;
		Properties props;
		props.SetProperty("h264.profile-level-id", plid);
		H264Encoder enc(props);
		ASSERT_EQ(enc.SetFrameRate(FPS, 500, 300), 1) << "[" << plid << "]";
		ASSERT_GE(enc.SetSize(W, H), 1) << "[" << plid << "]";
		VideoFrame* vf = EncodeOne(enc, seed);
		ASSERT_TRUE(vf != nullptr) << "[" << plid << "]";
		std::vector<BYTE> sps = SpsProfileBytes(vf);
		ASSERT_EQ(sps.size(), 3u) << "[" << plid << "]";
		EXPECT_EQ(sps[0], 0x42) << "[" << plid << "]";
		EXPECT_EQ(sps[1], 0x80) << "[" << plid << "]";
		EXPECT_EQ(sps[2], 0x1F) << "[" << plid << "]";
	}
}

// Un plid valide est conservé tel quel : profil high négocié (640c1f) => SPS
// portant exactement ces octets.
TEST(H264EncoderPlid, UnPlidValideEstConserve)
{
	DWORD seed = 48;
	Properties props;
	props.SetProperty("h264.profile-level-id", "640c1f");
	H264Encoder enc(props);
	ASSERT_EQ(enc.SetFrameRate(FPS, 500, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);
	VideoFrame* vf = EncodeOne(enc, seed);
	ASSERT_TRUE(vf != nullptr);
	std::vector<BYTE> sps = SpsProfileBytes(vf);
	ASSERT_EQ(sps.size(), 3u);
	EXPECT_EQ(sps[0], 0x64);
	EXPECT_EQ(sps[1], 0x0c);
	EXPECT_EQ(sps[2], 0x1f);
}
