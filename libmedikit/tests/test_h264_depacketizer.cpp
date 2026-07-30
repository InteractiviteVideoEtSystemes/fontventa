/**
 * test_h264_depacketizer.cpp — H264Depacketizer : réassemblage RTP → AVCC
 * (le format écrit tel quel comme échantillon MP4 par mp4save).
 *
 * Le cas adverse central reproduit une corruption observée en production : une
 * NALU fragmentée (FU-A) dont le bit E n'est jamais vu laissait son préfixe de
 * longueur à sa valeur d'attente. Celle-ci valait 1, soit `00 00 00 01` —
 * indistinguable d'un start code Annex-B : l'échantillon MP4 était
 * définitivement illisible (ffmpeg lui-même se désynchronise) alors que les
 * données H264 étaient intactes. Le dépaquetiseur referme désormais la NALU
 * dès qu'il sait qu'elle est terminée : E=1, nouvelle NALU, ou mark RTP.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/tools.h>
#include <h264/h264depacketizer.h>
#include <vector>
#include <cstring>

namespace
{

// SPS/PPS réels d'un enregistrement 640x480 Constrained Baseline
const BYTE kSps[] = {
	0x67, 0x42, 0xc0, 0x16, 0xb6, 0x80, 0xa0, 0x3d, 0xa1, 0x00, 0x00, 0x03,
	0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x16, 0x2e, 0xa0
};
const BYTE kPps[] = { 0x68, 0xce, 0x32, 0xc8 };

// Une NALU décrite par son type et sa taille, telle que lue dans l'AVCC.
struct Nalu
{
	BYTE  type;
	DWORD size;
};

// Parcourt une trame AVCC (préfixes de longueur sur 4 octets) et retourne la
// liste des NALU. Retourne false si la chaîne est incohérente — c'est
// exactement le test que fait VideoFrame::PacketizeH264 (et ffmpeg).
bool ParseAvcc(const BYTE * data, DWORD len, std::vector<Nalu> & out)
{
	out.clear();
	DWORD l = 0;
	while (l < len)
	{
		if (l + 4 > len) return false;
		DWORD sz = get4(data, l);
		l += 4;
		if (sz == 0 || l + sz > len) return false;
		Nalu n;
		n.type = data[l] & 0x1f;
		n.size = sz;
		out.push_back(n);
		l += sz;
	}
	return true;
}

// STAP-A portant SPS puis PPS (le paquet de tête habituel)
std::vector<BYTE> MakeStapA()
{
	std::vector<BYTE> p;
	p.push_back(0x78);                              // F=0, NRI=3, type=24
	p.push_back(0); p.push_back(sizeof(kSps));
	p.insert(p.end(), kSps, kSps + sizeof(kSps));
	p.push_back(0); p.push_back(sizeof(kPps));
	p.insert(p.end(), kPps, kPps + sizeof(kPps));
	return p;
}

// Fragment FU-A de `size` octets de charge utile, remplie d'un motif.
std::vector<BYTE> MakeFuA(BYTE nalType, bool S, bool E, DWORD size, BYTE fill)
{
	std::vector<BYTE> p;
	p.push_back(0x60 | 28);                         // F=0, NRI=3, type=28
	BYTE h = nalType & 0x1f;
	if (S) h |= 0x80;
	if (E) h |= 0x40;
	p.push_back(h);
	p.insert(p.end(), size, fill);
	return p;
}

// NALU complète en un seul paquet : la NALU résultante vaut 1 + dataSize
// (l'en-tête NAL fait partie de la NALU).
std::vector<BYTE> MakeSingle(BYTE nalType, DWORD dataSize, BYTE fill)
{
	std::vector<BYTE> p;
	p.push_back(0x60 | (nalType & 0x1f));
	p.insert(p.end(), dataSize, fill);
	return p;
}

} // namespace

// --- Nominal : STAP-A puis FU-A en 3 fragments, bit E posé ------------------
TEST(H264Depacketizer, StapAPuisFuAComplete)
{
	H264Depacketizer depak;
	std::vector<BYTE> stap = MakeStapA();
	std::vector<BYTE> f1 = MakeFuA(5, true,  false, 100, 0xAA);
	std::vector<BYTE> f2 = MakeFuA(5, false, false, 100, 0xBB);
	std::vector<BYTE> f3 = MakeFuA(5, false, true,   50, 0xCC);

	depak.AddPayload(&stap[0], stap.size(), false);
	depak.AddPayload(&f1[0], f1.size(), false);
	depak.AddPayload(&f2[0], f2.size(), false);
	MediaFrame * mf = depak.AddPayload(&f3[0], f3.size(), true);
	ASSERT_TRUE(mf != NULL);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus))
		<< "chaîne AVCC incohérente";
	ASSERT_EQ(nalus.size(), 3u);
	EXPECT_EQ(nalus[0].type, 7);  EXPECT_EQ(nalus[0].size, sizeof(kSps));
	EXPECT_EQ(nalus[1].type, 8);  EXPECT_EQ(nalus[1].size, sizeof(kPps));
	// NALU reconstruite = en-tête NAL (1 o) + les 3 charges utiles
	EXPECT_EQ(nalus[2].type, 5);  EXPECT_EQ(nalus[2].size, 1u + 250u);
	EXPECT_TRUE(((VideoFrame *)mf)->IsIntra());
}

// --- Adverse : FU-A dont le bit E n'est jamais posé, fin signalée par le mark
// C'est le cas qui produisait un échantillon MP4 corrompu.
TEST(H264Depacketizer, FuASansBitEFermeeParLeMark)
{
	H264Depacketizer depak;
	std::vector<BYTE> stap = MakeStapA();
	std::vector<BYTE> f1 = MakeFuA(5, true,  false, 100, 0xAA);
	std::vector<BYTE> f2 = MakeFuA(5, false, false,  50, 0xBB);

	depak.AddPayload(&stap[0], stap.size(), false);
	depak.AddPayload(&f1[0], f1.size(), false);
	MediaFrame * mf = depak.AddPayload(&f2[0], f2.size(), true);
	ASSERT_TRUE(mf != NULL);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus))
		<< "préfixe de longueur laissé à sa valeur d'attente";
	ASSERT_EQ(nalus.size(), 3u);
	EXPECT_EQ(nalus[2].type, 5);
	EXPECT_EQ(nalus[2].size, 1u + 150u);
	// Le préfixe ne doit jamais ressembler à un start code Annex-B
	DWORD off = 4 + sizeof(kSps) + 4 + sizeof(kPps);
	EXPECT_NE(get4(mf->GetData(), off), 1u);
}

// --- Non-régression : FU-A ouverte au-delà de l'offset 255 ------------------
// Reproduit la corruption constatée sur un enregistrement réel : la trame de
// tête porte SPS + PPS + un SEI volumineux, si bien que la NALU IDR fragmentée
// commence après le 255e octet. `set4` prenait alors un index BYTE : l'offset
// était replié modulo 256, la taille était écrite au milieu du SEI et le
// préfixe de longueur de l'IDR restait à sa valeur d'attente. Seul le PREMIER
// échantillon du fichier était touché (les trames P n'ont rien devant leur
// NALU fragmentée), d'où un mp4play qui échouait dès la première trame.
TEST(H264Depacketizer, FuAOuverteAuDelaDeLOffset255)
{
	H264Depacketizer depak;
	std::vector<BYTE> stap = MakeStapA();
	std::vector<BYTE> sei  = MakeSingle(6, 693, 0x11);            // SEI, 694 o
	std::vector<BYTE> f1   = MakeFuA(5, true,  false, 1300, 0xAA);
	std::vector<BYTE> f2   = MakeFuA(5, false, true,  1300, 0xBB);

	depak.AddPayload(&stap[0], stap.size(), false);
	depak.AddPayload(&sei[0], sei.size(), false);
	depak.AddPayload(&f1[0], f1.size(), false);
	MediaFrame * mf = depak.AddPayload(&f2[0], f2.size(), true);
	ASSERT_TRUE(mf != NULL);

	// L'IDR commence bien au-delà du 255e octet
	DWORD offIdr = 4 + sizeof(kSps) + 4 + sizeof(kPps) + 4 + 694;
	ASSERT_GT(offIdr, 255u);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus))
		<< "chaîne AVCC rompue : taille écrite à un offset replié modulo 256";
	ASSERT_EQ(nalus.size(), 4u);
	EXPECT_EQ(nalus[2].type, 6);  EXPECT_EQ(nalus[2].size, 694u);
	EXPECT_EQ(nalus[3].type, 5);  EXPECT_EQ(nalus[3].size, 1u + 2600u);
	EXPECT_EQ(get4(mf->GetData(), offIdr), 1u + 2600u);

	// Le SEI n'a pas été écrasé par une écriture à l'offset replié
	const BYTE * sd = mf->GetData() + 4 + sizeof(kSps) + 4 + sizeof(kPps) + 4;
	for (DWORD k = 1; k < 694; k++)
		ASSERT_EQ(sd[k], 0x11) << "octet " << k << " du SEI écrasé";
}

// --- Adverse : FU-A restée ouverte, refermée par l'arrivée d'une autre NALU --
TEST(H264Depacketizer, FuAFermeeParNaluSuivante)
{
	H264Depacketizer depak;
	std::vector<BYTE> f1  = MakeFuA(1, true, false, 80, 0xAA);   // FU-A sans E
	std::vector<BYTE> nxt = MakeSingle(1, 40, 0xDD);             // NALU suivante

	depak.AddPayload(&f1[0], f1.size(), false);
	MediaFrame * mf = depak.AddPayload(&nxt[0], nxt.size(), true);
	ASSERT_TRUE(mf != NULL);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus));
	ASSERT_EQ(nalus.size(), 2u);
	EXPECT_EQ(nalus[0].size, 1u + 80u);
	EXPECT_EQ(nalus[1].size, 1u + 40u);
}

// --- Adverse : FU-A ouverte sans aucune donnée (S=1 seul, puis mark) --------
// Une NALU vide est invalide en AVCC : elle doit être retirée, pas écrite avec
// une longueur nulle.
TEST(H264Depacketizer, FuAOuverteSansDonnee)
{
	H264Depacketizer depak;
	std::vector<BYTE> stap = MakeStapA();
	std::vector<BYTE> f1   = MakeFuA(5, true, false, 0, 0);      // S=1, 0 octet

	depak.AddPayload(&stap[0], stap.size(), false);
	MediaFrame * mf = depak.AddPayload(&f1[0], f1.size(), true);
	ASSERT_TRUE(mf != NULL);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus))
		<< "NALU de longueur nulle laissée dans la chaîne AVCC";
	// L'en-tête NAL reconstruit compte pour 1 octet : la NALU n'est pas vide.
	ASSERT_EQ(nalus.size(), 3u);
	EXPECT_EQ(nalus[2].size, 1u);
}

// --- Réinitialisation entre deux trames -------------------------------------
TEST(H264Depacketizer, ResetFramePurgeLEtat)
{
	H264Depacketizer depak;
	std::vector<BYTE> f1 = MakeFuA(5, true, false, 100, 0xAA);
	depak.AddPayload(&f1[0], f1.size(), false);
	depak.ResetFrame();

	std::vector<BYTE> nxt = MakeSingle(1, 30, 0xEE);
	MediaFrame * mf = depak.AddPayload(&nxt[0], nxt.size(), true);
	ASSERT_TRUE(mf != NULL);

	std::vector<Nalu> nalus;
	ASSERT_TRUE(ParseAvcc(mf->GetData(), mf->GetLength(), nalus));
	ASSERT_EQ(nalus.size(), 1u);
	EXPECT_EQ(nalus[0].size, 1u + 30u);
	EXPECT_FALSE(((VideoFrame *)mf)->IsIntra());
}
