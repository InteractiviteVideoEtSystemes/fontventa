/**
 * test_vp8_depacketizer.cpp — VP8Depacketizer : réassemblage RTP (RFC 7741) →
 * trame VP8 complète, telle que la voie d'enregistrement la consomme.
 *
 * Ce que le dépaquetiseur doit garantir (et que ces tests verrouillent) :
 *  - le payload descriptor est retiré, quelle que soit sa forme (1 octet nu,
 *    étendu avec PictureID 1 ou 2 octets, TL0PICIDX, TID/KEYIDX) ;
 *  - le bit frame_type du payload header est traduit en drapeau intra — c'est
 *    lui que waitVideo (mp4writer) et la création de piste MP4 attendent ; sans
 *    lui, l'enregistrement d'un appel VP8 restait éternellement en attente et
 *    produisait un fichier de durée nulle (constat du 2026-08-15) ;
 *  - les dimensions du keyframe header non compressé sont extraites ;
 *  - une trame dont le paquet de tête (S=1, PID=0) n'a pas été vu est REJETÉE :
 *    un échantillon vidéo incomplet est indécodable, mieux vaut rien que lui.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <vp8/vp8depacketizer.h>
#include <vector>

namespace
{

// Payload header d'une trame clé : frame tag (3 octets, frame_type=0,
// show_frame=1), start code 9d 01 2a, largeur et hauteur little-endian
// (14 bits utiles), puis `extra` octets de pseudo-données.
std::vector<BYTE> MakeKeyframePayload(WORD width, WORD height, size_t extra)
{
	std::vector<BYTE> p;
	p.push_back(0x10);	// frame_type=0 (clé), version=0, show_frame=1
	p.push_back(0x00);
	p.push_back(0x00);
	p.push_back(0x9d);
	p.push_back(0x01);
	p.push_back(0x2a);
	p.push_back(width & 0xff);
	p.push_back((width >> 8) & 0x3f);
	p.push_back(height & 0xff);
	p.push_back((height >> 8) & 0x3f);
	for (size_t i = 0; i < extra; i++)
		p.push_back((BYTE)(i & 0xff));
	return p;
}

// Pseudo-données d'une trame inter (frame_type=1) ou d'une continuation.
std::vector<BYTE> MakeBytes(BYTE first, size_t count)
{
	std::vector<BYTE> p;
	p.push_back(first);
	for (size_t i = 1; i < count; i++)
		p.push_back((BYTE)(0xA0 + (i & 0x0f)));
	return p;
}

// Concatène descriptor + payload en un paquet RTP (partie média).
std::vector<BYTE> Packet(const std::vector<BYTE> & desc, const std::vector<BYTE> & payload)
{
	std::vector<BYTE> pkt(desc);
	pkt.insert(pkt.end(), payload.begin(), payload.end());
	return pkt;
}

const std::vector<BYTE> kDescStart = { 0x10 };	// S=1, PID=0, sans extension
const std::vector<BYTE> kDescCont  = { 0x00 };	// continuation (S=0)

} // namespace

TEST(VP8Depacketizer, KeyframeMonoPaquet)
{
	VP8Depacketizer depak;
	std::vector<BYTE> payload = MakeKeyframePayload(640, 480, 20);
	std::vector<BYTE> pkt = Packet(kDescStart, payload);

	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt.data(), pkt.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	EXPECT_TRUE(frame->IsIntra());
	EXPECT_EQ(frame->GetWidth(), 640u);
	EXPECT_EQ(frame->GetHeight(), 480u);
	ASSERT_EQ(frame->GetLength(), payload.size());
	EXPECT_EQ(memcmp(frame->GetData(), payload.data(), payload.size()), 0);
	EXPECT_EQ(frame->GetCodec(), VideoCodec::VP8);
}

TEST(VP8Depacketizer, TrameInterNonIntra)
{
	VP8Depacketizer depak;
	// frame_type=1 : trame inter
	std::vector<BYTE> payload = MakeBytes(0x11, 15);
	std::vector<BYTE> pkt = Packet(kDescStart, payload);

	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt.data(), pkt.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	EXPECT_FALSE(frame->IsIntra());
	EXPECT_EQ(frame->GetWidth(), 0u);
	EXPECT_EQ(frame->GetLength(), payload.size());
}

TEST(VP8Depacketizer, DescripteurEtenduRetire)
{
	VP8Depacketizer depak;
	// X=1 + S=1 ; extensions I (PictureID 2 octets, M=1), L, T → 6 octets
	std::vector<BYTE> desc = { 0x90, 0xE0, 0x81, 0x23, 0x07, 0x1F };
	std::vector<BYTE> payload = MakeKeyframePayload(320, 240, 8);
	std::vector<BYTE> pkt = Packet(desc, payload);

	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt.data(), pkt.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	EXPECT_TRUE(frame->IsIntra());
	EXPECT_EQ(frame->GetWidth(), 320u);
	EXPECT_EQ(frame->GetHeight(), 240u);
	ASSERT_EQ(frame->GetLength(), payload.size());
	EXPECT_EQ(memcmp(frame->GetData(), payload.data(), payload.size()), 0);
}

TEST(VP8Depacketizer, FragmentsAssembles)
{
	VP8Depacketizer depak;
	std::vector<BYTE> head = MakeKeyframePayload(176, 144, 30);
	std::vector<BYTE> tail = MakeBytes(0x55, 25);
	std::vector<BYTE> pkt1 = Packet(kDescStart, head);
	std::vector<BYTE> pkt2 = Packet(kDescCont, tail);

	ASSERT_NE(depak.AddPayload(pkt1.data(), pkt1.size(), false), (MediaFrame *)NULL);
	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt2.data(), pkt2.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	// Le drapeau intra posé sur le paquet de tête survit aux continuations
	EXPECT_TRUE(frame->IsIntra());
	ASSERT_EQ(frame->GetLength(), head.size() + tail.size());
	EXPECT_EQ(memcmp(frame->GetData(), head.data(), head.size()), 0);
	EXPECT_EQ(memcmp(frame->GetData() + head.size(), tail.data(), tail.size()), 0);
}

TEST(VP8Depacketizer, ArriveeEnMilieuDeTrameIgnoree)
{
	VP8Depacketizer depak;
	// Continuation sans jamais avoir vu de paquet de tête (arrivée en cours de
	// flux) : rien d'exploitable, rien d'accumulé.
	std::vector<BYTE> pkt = Packet(kDescCont, MakeBytes(0x42, 10));
	EXPECT_EQ(depak.AddPayload(pkt.data(), pkt.size(), true), (MediaFrame *)NULL);

	// La trame suivante, complète, passe normalement.
	std::vector<BYTE> payload = MakeKeyframePayload(640, 480, 12);
	std::vector<BYTE> pkt2 = Packet(kDescStart, payload);
	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt2.data(), pkt2.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	EXPECT_TRUE(frame->IsIntra());
	EXPECT_EQ(frame->GetLength(), payload.size());
}

TEST(VP8Depacketizer, NouvelleTrameJetteLaPrecedenteInachevee)
{
	VP8Depacketizer depak;
	// Trame 1 : tête sans fin (mark perdu)
	std::vector<BYTE> pkt1 = Packet(kDescStart, MakeBytes(0x11, 40));
	ASSERT_NE(depak.AddPayload(pkt1.data(), pkt1.size(), false), (MediaFrame *)NULL);

	// Trame 2 : nouveau paquet de tête → la trame 1 est abandonnée, pas collée
	std::vector<BYTE> payload = MakeKeyframePayload(352, 288, 16);
	std::vector<BYTE> pkt2 = Packet(kDescStart, payload);
	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt2.data(), pkt2.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	EXPECT_TRUE(frame->IsIntra());
	ASSERT_EQ(frame->GetLength(), payload.size());
	EXPECT_EQ(memcmp(frame->GetData(), payload.data(), payload.size()), 0);
}

TEST(VP8Depacketizer, DescripteurTronqueRejete)
{
	VP8Depacketizer depak;
	// X=1 annonce un octet d'extension qui n'est pas là
	BYTE xOnly[] = { 0x90 };
	EXPECT_EQ(depak.AddPayload(xOnly, sizeof(xOnly), true), (MediaFrame *)NULL);

	// I=1 annonce un PictureID absent
	BYTE noPicId[] = { 0x90, 0x80 };
	EXPECT_EQ(depak.AddPayload(noPicId, sizeof(noPicId), true), (MediaFrame *)NULL);

	// PictureID long (M=1) coupé au milieu
	BYTE cutPicId[] = { 0x90, 0x80, 0x81 };
	EXPECT_EQ(depak.AddPayload(cutPicId, sizeof(cutPicId), true), (MediaFrame *)NULL);
}

TEST(VP8Depacketizer, PaquetSansDonneesRejete)
{
	VP8Depacketizer depak;
	// Descripteur seul, aucune donnée VP8 derrière
	BYTE descOnly[] = { 0x10 };
	EXPECT_EQ(depak.AddPayload(descOnly, sizeof(descOnly), true), (MediaFrame *)NULL);
	EXPECT_EQ(depak.AddPayload((BYTE *)NULL, 0, true), (MediaFrame *)NULL);
}

TEST(VP8Depacketizer, ResetFrameRepartAZero)
{
	VP8Depacketizer depak;
	std::vector<BYTE> payload = MakeKeyframePayload(640, 480, 10);
	std::vector<BYTE> pkt = Packet(kDescStart, payload);
	VideoFrame * frame = (VideoFrame *)depak.AddPayload(pkt.data(), pkt.size(), true);
	ASSERT_NE(frame, (VideoFrame *)NULL);
	depak.ResetFrame();
	EXPECT_EQ(frame->GetLength(), 0u);
	EXPECT_FALSE(frame->IsIntra());

	// Après reset, une continuation orpheline est bien re-rejetée
	std::vector<BYTE> cont = Packet(kDescCont, MakeBytes(0x42, 5));
	EXPECT_EQ(depak.AddPayload(cont.data(), cont.size(), true), (MediaFrame *)NULL);
}
