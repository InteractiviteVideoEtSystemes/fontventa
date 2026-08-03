/**
 * test_tools.cpp — accesseurs bitstream de medkit/tools.h (get1..get8 /
 * set1..set8).
 *
 * Leur paramètre d'index était historiquement un BYTE : tout offset >= 256
 * était silencieusement replié modulo 256, écrivant ou lisant au mauvais
 * endroit sans le moindre diagnostic. Ce défaut a réellement corrompu des
 * échantillons MP4 (préfixe de longueur AVCC d'une NALU située au-delà du 256e
 * octet d'une trame). Ces tests verrouillent l'adressage au-delà de 255.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/tools.h>
#include <vector>

// --- Aller-retour à un offset supérieur à 255 -------------------------------
TEST(Tools, Set4Get4AuDelaDe255)
{
	std::vector<BYTE> buf(2048, 0);

	set4(&buf[0], 733, 5582);
	EXPECT_EQ(get4(&buf[0], 733), 5582u);

	// Rien n'a été écrit à l'offset replié modulo 256
	EXPECT_EQ(get4(&buf[0], 733 & 0xFF), 0u);
}

TEST(Tools, TousLesAccesseursAuDelaDe255)
{
	std::vector<BYTE> buf(4096, 0);
	const DWORD off = 1000;

	set1(&buf[0], off, 0xAB);
	EXPECT_EQ(get1(&buf[0], off), 0xAB);

	set2(&buf[0], off, 0x1234);
	EXPECT_EQ(get2(&buf[0], off), 0x1234u);

	set3(&buf[0], off, 0x123456);
	EXPECT_EQ(get3(&buf[0], off), 0x123456u);

	set4(&buf[0], off, 0x12345678);
	EXPECT_EQ(get4(&buf[0], off), 0x12345678u);

	// get8 doit rendre un QWORD (il retournait un DWORD : moitié haute perdue)
	set8(&buf[0], off, (QWORD)0x0123456789ABCDEFULL);
	EXPECT_EQ(get8(&buf[0], off), (QWORD)0x0123456789ABCDEFULL);
}

// --- Ordre gros-boutiste (contrat des formats RTP / MP4) --------------------
TEST(Tools, OrdreGrosBoutiste)
{
	BYTE buf[8] = { 0 };

	set4(buf, 0, 0x11223344);
	EXPECT_EQ(buf[0], 0x11);
	EXPECT_EQ(buf[1], 0x22);
	EXPECT_EQ(buf[2], 0x33);
	EXPECT_EQ(buf[3], 0x44);

	set3(buf, 0, 0xAABBCC);
	EXPECT_EQ(buf[0], 0xAA);
	EXPECT_EQ(buf[1], 0xBB);
	EXPECT_EQ(buf[2], 0xCC);

	set2(buf, 0, 0xDEAD);
	EXPECT_EQ(buf[0], 0xDE);
	EXPECT_EQ(buf[1], 0xAD);
}
