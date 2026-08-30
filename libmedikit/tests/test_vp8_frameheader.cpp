/**
 * test_vp8_frameheader.cpp — le parseur d'en-tête VP8 (RFC 6386) et
 * l'acquittement RPSI du décodeur (GetReferencePictureId).
 *
 * Témoins :
 *  - un encodeur booléen écrit d'après le §7.2 de la RFC (algorithme DISTINCT
 *    du décodeur §7.3) fabrique des en-têtes inter aux drapeaux choisis ;
 *  - des trames RÉELLES produites par libvpx (VP8Encoder) doivent toutes se
 *    parser sans overrun, la trame clé doit rafraîchir les deux références,
 *    et le drapeau keyFrame doit coïncider avec l'intra vu par ffmpeg.
 *  Limite assumée : l'ORDRE des champs inter n'a pas d'oracle exécutable local
 *  (libvpx n'expose pas ses drapeaux) — il a été vérifié sur le texte de la
 *  RFC, et la recette réelle (chute des trames clés Linphone) le prouvera.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <vp8/vp8frameheader.h>
#include <vp8/vp8depacketizer.h>
#include <vp8/vp8decoder.h>
#include <vp8/vp8encoder.h>
#include <vector>

namespace {

const int W = 320, H = 240;

// Encodeur booléen RFC 6386 §7.2, à seule fin de fabriquer des en-têtes.
class BoolEncoder
{
public:
	void Bit(int v)
	{
		DWORD split = 1 + (((range - 1) * 128) >> 8);
		if (v)
		{
			bottom += split;
			range -= split;
		}
		else
			range = split;
		while (range < 128)
		{
			range <<= 1;
			if (bottom & 0x80000000u)
				Carry();
			bottom <<= 1;
			if (!--bitCount)
			{
				out.push_back(BYTE(bottom >> 24));
				bottom &= 0x00FFFFFFu;
				bitCount = 8;
			}
		}
	}

	void Literal(DWORD v, int bits)
	{
		while (bits--)
			Bit((v >> bits) & 1);
	}

	std::vector<BYTE> Flush()
	{
		// 64 bits nuls poussent dehors tous les octets utiles ; le décodeur
		// s'arrête avant d'atteindre ce remplissage.
		for (int i = 0; i < 64; i++)
			Bit(0);
		return out;
	}

private:
	void Carry()
	{
		for (size_t i = out.size(); i-- > 0;)
		{
			if (out[i] != 255)
			{
				out[i]++;
				return;
			}
			out[i] = 0;
		}
	}

	std::vector<BYTE> out;
	DWORD range = 255;
	DWORD bottom = 0;
	int bitCount = 24;
};

// Trame inter minimale (ou avec tous les blocs optionnels si withOptional) :
// le chemin jusqu'au §9.7 avec les drapeaux demandés.
std::vector<BYTE> InterFrame(bool refreshGolden, bool refreshAlt,
                             int copyGf, int copyArf, bool withOptional = false)
{
	BoolEncoder e;
	// §9.3 segmentation
	if (!withOptional)
		e.Bit(0);
	else
	{
		e.Bit(1);
		e.Bit(1);	// update_mb_segmentation_map
		e.Bit(1);	// update_segment_feature_data
		e.Bit(0);	// segment_feature_mode
		for (int i = 0; i < 4; i++) { e.Bit(1); e.Literal(0x55, 7); e.Bit(1); }
		for (int i = 0; i < 4; i++) { e.Bit(1); e.Literal(0x2A, 6); e.Bit(0); }
		for (int i = 0; i < 3; i++) { e.Bit(1); e.Literal(0xA5, 8); }
	}
	// §9.4 loop filter
	e.Bit(0);
	e.Literal(withOptional ? 63 : 0, 6);
	e.Literal(withOptional ? 7 : 0, 3);
	if (!withOptional)
		e.Bit(0);
	else
	{
		e.Bit(1);	// loop_filter_adj_enable
		e.Bit(1);	// mode_ref_lf_delta_update
		for (int i = 0; i < 8; i++) { e.Bit(1); e.Literal(0x15, 6); e.Bit(1); }
	}
	// §9.5 partitions de tokens
	e.Literal(0, 2);
	// §9.6 quantification
	e.Literal(withOptional ? 0x7F : 0, 7);
	for (int i = 0; i < 5; i++)
	{
		if (withOptional) { e.Bit(1); e.Literal(0xF, 4); e.Bit(0); }
		else e.Bit(0);
	}
	// §9.7 références
	e.Bit(refreshGolden);
	e.Bit(refreshAlt);
	if (!refreshGolden)
		e.Literal(copyGf, 2);
	if (!refreshAlt)
		e.Literal(copyArf, 2);

	std::vector<BYTE> part = e.Flush();
	// Frame tag : inter (bit0=1), version 0, show=1, taille de la partition
	DWORD tag = 0x01 | (1 << 4) | (DWORD(part.size()) << 5);
	std::vector<BYTE> frame = { BYTE(tag), BYTE(tag >> 8), BYTE(tag >> 16) };
	frame.insert(frame.end(), part.begin(), part.end());
	return frame;
}

std::vector<BYTE> KeyFrameBytes()
{
	DWORD tag = 0x00 | (1 << 4) | (8u << 5);	// keyframe, show=1
	return { BYTE(tag), BYTE(tag >> 8), BYTE(tag >> 16),
	         0x9d, 0x01, 0x2a,
	         0x40, 0x01,	// largeur 320, échelle 0
	         0xf0, 0x00,	// hauteur 240, échelle 0
	         0, 0, 0, 0, 0, 0, 0, 0 };
}

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

// Payload RTP : descripteur RFC 7741 (S=1, PID=0, PictureID optionnel) + trame
std::vector<BYTE> WithDescriptor(const BYTE* data, DWORD size, bool withPid, WORD pid)
{
	std::vector<BYTE> p;
	if (withPid)
	{
		p.push_back(0x90);	// X=1, S=1
		p.push_back(0x80);	// I=1
		if (pid & 0x8000)
		{
			p.push_back(BYTE(pid >> 8));
			p.push_back(BYTE(pid));
		}
		else
			p.push_back(BYTE(pid & 0x7F));
	}
	else
		p.push_back(0x10);	// S=1, sans extension
	p.insert(p.end(), data, data + size);
	return p;
}

} // namespace

TEST(VP8FrameHeader, TrameCleRafraichitLesDeuxReferences)
{
	std::vector<BYTE> f = KeyFrameBytes();
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_TRUE(info.keyFrame);
	EXPECT_TRUE(info.showFrame);
	EXPECT_TRUE(info.refreshGolden);
	EXPECT_TRUE(info.refreshAltRef);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, TrameCleSansStartCodeRefusee)
{
	std::vector<BYTE> f = KeyFrameBytes();
	f[3] = 0x00;
	VP8FrameHeaderInfo info;
	EXPECT_FALSE(VP8ParseFrameHeader(f.data(), f.size(), info));
}

TEST(VP8FrameHeader, TropCourtRefuse)
{
	std::vector<BYTE> f = KeyFrameBytes();
	VP8FrameHeaderInfo info;
	EXPECT_FALSE(VP8ParseFrameHeader(nullptr, 0, info));
	EXPECT_FALSE(VP8ParseFrameHeader(f.data(), 2, info));
	EXPECT_FALSE(VP8ParseFrameHeader(f.data(), 9, info));	// trame clé amputée
}

TEST(VP8FrameHeader, InterSansMiseAJour)
{
	std::vector<BYTE> f = InterFrame(false, false, 0, 0);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_FALSE(info.keyFrame);
	EXPECT_TRUE(info.showFrame);
	EXPECT_FALSE(info.UpdatesReference());
}

TEST(VP8FrameHeader, InterRefreshGolden)
{
	std::vector<BYTE> f = InterFrame(true, false, 0, 0);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_TRUE(info.refreshGolden);
	EXPECT_FALSE(info.refreshAltRef);
	EXPECT_EQ(info.copyToAltRef, 0);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, InterRefreshAltRef)
{
	std::vector<BYTE> f = InterFrame(false, true, 0, 0);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_FALSE(info.refreshGolden);
	EXPECT_TRUE(info.refreshAltRef);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, InterCopieVersGoldenCompteCommeMiseAJour)
{
	std::vector<BYTE> f = InterFrame(false, false, 1, 0);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_FALSE(info.refreshGolden);
	EXPECT_EQ(info.copyToGolden, 1);
	EXPECT_EQ(info.copyToAltRef, 0);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, InterCopieVersAltRefCompteCommeMiseAJour)
{
	std::vector<BYTE> f = InterFrame(false, false, 0, 2);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_EQ(info.copyToGolden, 0);
	EXPECT_EQ(info.copyToAltRef, 2);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, InterAvecBlocsOptionnels)
{
	// Toutes les branches à sauter sont armées (segmentation, deltas de loop
	// filter, deltas de quantification) : une largeur de champ fausse dans le
	// parseur décale les drapeaux du §9.7 et le test tombe.
	std::vector<BYTE> f = InterFrame(false, true, 0, 0, true);
	VP8FrameHeaderInfo info;
	ASSERT_TRUE(VP8ParseFrameHeader(f.data(), f.size(), info));
	EXPECT_FALSE(info.refreshGolden);
	EXPECT_TRUE(info.refreshAltRef);
	EXPECT_TRUE(info.UpdatesReference());
}

TEST(VP8FrameHeader, PartitionTropCourteRefusee)
{
	// firstPartSize réécrit à 1 : le décodeur borné manque de bits (overrun)
	std::vector<BYTE> f = InterFrame(true, true, 0, 0);
	DWORD tag = 0x01 | (1 << 4) | (1u << 5);
	f[0] = BYTE(tag); f[1] = BYTE(tag >> 8); f[2] = BYTE(tag >> 16);
	VP8FrameHeaderInfo info;
	EXPECT_FALSE(VP8ParseFrameHeader(f.data(), f.size(), info));

	// firstPartSize plus grand que la trame
	std::vector<BYTE> g = InterFrame(true, true, 0, 0);
	tag = 0x01 | (1 << 4) | (DWORD(g.size()) << 5);
	g[0] = BYTE(tag); g[1] = BYTE(tag >> 8); g[2] = BYTE(tag >> 16);
	EXPECT_FALSE(VP8ParseFrameHeader(g.data(), g.size(), info));

	// firstPartSize nul
	tag = 0x01 | (1 << 4);
	g[0] = BYTE(tag); g[1] = BYTE(tag >> 8); g[2] = BYTE(tag >> 16);
	EXPECT_FALSE(VP8ParseFrameHeader(g.data(), g.size(), info));
}

TEST(VP8Descriptor, PictureId15Bits)
{
	BYTE d[] = { 0x90, 0x80, 0x81, 0x23, 0xAA };
	WORD pid = 0;
	ASSERT_TRUE(VP8DescriptorPictureId(d, sizeof(d), pid));
	EXPECT_EQ(pid, 0x8123);
}

TEST(VP8Descriptor, PictureId7Bits)
{
	BYTE d[] = { 0x90, 0x80, 0x45, 0xAA };
	WORD pid = 0;
	ASSERT_TRUE(VP8DescriptorPictureId(d, sizeof(d), pid));
	EXPECT_EQ(pid, 0x45);
}

TEST(VP8Descriptor, PictureIdAbsentOuTronque)
{
	WORD pid = 0;
	BYTE sansX[] = { 0x10, 0xAA, 0xBB };
	EXPECT_FALSE(VP8DescriptorPictureId(sansX, sizeof(sansX), pid));
	BYTE sansI[] = { 0x90, 0x40, 0xAA };	// X=1 mais I=0 (L=1)
	EXPECT_FALSE(VP8DescriptorPictureId(sansI, sizeof(sansI), pid));
	BYTE tronque15[] = { 0x90, 0x80, 0x81 };	// M=1 mais un seul octet
	EXPECT_FALSE(VP8DescriptorPictureId(tronque15, sizeof(tronque15), pid));
	BYTE vide[] = { 0x90, 0x80 };
	EXPECT_FALSE(VP8DescriptorPictureId(vide, sizeof(vide), pid));
}

TEST(VP8FrameHeader, TramesReellesLibvpx)
{
	if (!VP8Encoder::IsSupported())
		GTEST_SKIP() << "encodeur VP8 absent";
	DWORD seed = 7;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 1000, 10), 1);	// une clé toutes les 10
	ASSERT_GE(enc.SetSize(W, H), 1);

	int frames = 0, keyframes = 0, updates = 0;
	for (int i = 0; i < 40; i++)
	{
		VideoFramePtr f = enc.EncodeFrame(CreateNoise(seed));
		if (!f)
			continue;
		frames++;
		VP8FrameHeaderInfo info;
		ASSERT_TRUE(VP8ParseFrameHeader(f->GetData(), f->GetLength(), info))
			<< "trame reelle " << i << " illisible";
		// Le drapeau keyFrame du parseur doit coïncider avec l'intra ffmpeg
		EXPECT_EQ(info.keyFrame, f->IsIntra()) << "trame " << i;
		if (info.keyFrame)
		{
			keyframes++;
			EXPECT_TRUE(info.refreshGolden);
			EXPECT_TRUE(info.refreshAltRef);
		}
		if (info.UpdatesReference())
			updates++;
	}
	ASSERT_GT(frames, 10);
	EXPECT_GE(keyframes, 2);	// intraPeriod=10 sur ~40 trames
	EXPECT_GE(updates, keyframes);
	RecordProperty("frames", frames);
	RecordProperty("updates", updates);
}

TEST(VP8DecoderRPSI, AcquitteLaTrameCleDecodee)
{
	if (!VP8Encoder::IsSupported())
		GTEST_SKIP() << "encodeur VP8 absent";
	DWORD seed = 3;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 1000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);
	VideoFramePtr key;
	for (int i = 0; i < 5 && !key; i++)
		key = enc.EncodeFrame(CreateNoise(seed));
	ASSERT_NE(key, nullptr);
	ASSERT_TRUE(key->IsIntra());

	VP8Decoder dec;
	std::vector<BYTE> p = WithDescriptor(key->GetData(), key->GetLength(), true, 0x8123);
	ASSERT_TRUE(dec.DecodePacket(p.data(), p.size(), 0, 1));
	WORD pid = 0;
	ASSERT_TRUE(dec.GetReferencePictureId(pid));
	EXPECT_EQ(pid, 0x8123);
}

TEST(VP8DecoderRPSI, PasDAcquittementSansPictureId)
{
	if (!VP8Encoder::IsSupported())
		GTEST_SKIP() << "encodeur VP8 absent";
	DWORD seed = 3;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 1000, 300), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);
	VideoFramePtr key;
	for (int i = 0; i < 5 && !key; i++)
		key = enc.EncodeFrame(CreateNoise(seed));
	ASSERT_NE(key, nullptr);

	VP8Decoder dec;
	std::vector<BYTE> p = WithDescriptor(key->GetData(), key->GetLength(), false, 0);
	ASSERT_TRUE(dec.DecodePacket(p.data(), p.size(), 0, 1));
	WORD pid = 0;
	EXPECT_FALSE(dec.GetReferencePictureId(pid));
}

TEST(VP8DecoderRPSI, PasDAcquittementSiLeDecodageEchoue)
{
	// En-tête inter valide qui annonce un refresh golden, mais sans
	// macroblocs : ffmpeg refuse, donc pas d'acquittement (le RPSI désigne
	// une référence que le récepteur POSSÈDE).
	std::vector<BYTE> junk = InterFrame(true, true, 0, 0);
	VP8Decoder dec;
	std::vector<BYTE> p = WithDescriptor(junk.data(), junk.size(), true, 0x8001);
	dec.DecodePacket(p.data(), p.size(), 0, 1);
	WORD pid = 0;
	EXPECT_FALSE(dec.GetReferencePictureId(pid));
}

TEST(VP8DecoderRPSI, CoherentAvecLeParseurSurUnFluxReel)
{
	if (!VP8Encoder::IsSupported())
		GTEST_SKIP() << "encodeur VP8 absent";
	DWORD seed = 11;
	VP8Encoder enc((Properties()));
	ASSERT_EQ(enc.SetFrameRate(30, 1000, 10), 1);
	ASSERT_GE(enc.SetSize(W, H), 1);

	VP8Decoder dec;
	WORD next = 0x8000;
	int checked = 0;
	for (int i = 0; i < 40; i++)
	{
		VideoFramePtr f = enc.EncodeFrame(CreateNoise(seed));
		if (!f)
			continue;
		WORD sent = next++;
		std::vector<BYTE> p = WithDescriptor(f->GetData(), f->GetLength(), true, sent);
		if (!dec.DecodePacket(p.data(), p.size(), 0, 1))
			continue;
		VP8FrameHeaderInfo info;
		ASSERT_TRUE(VP8ParseFrameHeader(f->GetData(), f->GetLength(), info));
		WORD pid = 0;
		bool ack = dec.GetReferencePictureId(pid);
		EXPECT_EQ(ack, info.UpdatesReference()) << "trame " << i;
		if (ack)
			EXPECT_EQ(pid, sent);
		checked++;
	}
	ASSERT_GT(checked, 10);
}
