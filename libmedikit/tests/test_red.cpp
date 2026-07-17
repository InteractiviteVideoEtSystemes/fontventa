/**
 * test_red.cpp — RTPRedundantPayload::ParseRed (RED, RFC 2198/4103) : parsing
 * d'un paquet forgé + cas adverses. ParseRed a été DURCI (red.cpp) pour borner
 * ses accès ; les cas adverses vérifient qu'un RED malformé est rejeté dans un
 * état sûr (aucune redondance, pas de payload primaire) au lieu de lire hors
 * limites.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/red.h>
#include <cstring>

// --- Nominal : 1 bloc redondant + payload primaire --------------------------
// Layout (cf. red.cpp) : header redondant 4 octets, octet type primaire, puis
// payload redondant (skip octets), puis payload primaire.
//   b0 = F(1)|type(7) = 0x80|100 = 0xE4
//   b1,b2 = offset 14 bits (=160) + 2 bits hauts de sz ; b3 = 8 bits bas de sz
//     offset=160 -> b1=160>>6=2 ; (160&0x3F)<<2 = 0x80 ; sz=3 -> b2=0x80, b3=3
//   b4 = type primaire, F=0 -> 98 (0x62)
TEST(Red, ParsePaquetValide)
{
	BYTE pkt[] = {
		0xE4, 0x02, 0x80, 0x03, // header redondant : type=100, offset=160, sz=3
		0x62,                   // type primaire=98, dernier bloc
		'R','E','D',            // payload redondant (3 o)
		'P','R','I'             // payload primaire (3 o)
	};
	RTPRedundantPayload red(pkt, sizeof(pkt));

	ASSERT_EQ(red.GetRedundantCount(), 1);
	EXPECT_EQ(red.GetRedundantType(0), 100);
	EXPECT_EQ(red.GetRedundantPayloadSize(0), 3u);
	ASSERT_TRUE(red.GetRedundantPayloadData(0) != NULL);
	EXPECT_EQ(0, memcmp(red.GetRedundantPayloadData(0), "RED", 3));

	EXPECT_EQ(red.GetPrimaryType(), 98);
	EXPECT_EQ(red.GetPrimaryPayloadSize(), 3u);
	ASSERT_TRUE(red.GetPrimaryPayloadData() != NULL);
	EXPECT_EQ(0, memcmp(red.GetPrimaryPayloadData(), "PRI", 3));
}

// --- Nominal : primaire seul (aucune redondance) ----------------------------
TEST(Red, ParsePrimaireSeul)
{
	BYTE pkt[] = { 0x62, 'H','I' }; // type primaire=98 (F=0), 2 o de payload
	RTPRedundantPayload red(pkt, sizeof(pkt));

	EXPECT_EQ(red.GetRedundantCount(), 0);
	EXPECT_EQ(red.GetPrimaryType(), 98);
	EXPECT_EQ(red.GetPrimaryPayloadSize(), 2u);
	EXPECT_EQ(0, memcmp(red.GetPrimaryPayloadData(), "HI", 2));
}

// ============================================================================
// Cas adverses — RED malformé. Sans le durcissement, ces entrées lisaient
// hors limites. On vérifie l'absence de crash + l'état sûr.
// ============================================================================

// Constructeur avec buffer nul / taille nulle : rien parsé, pas de payload.
TEST(Red, ConstructeurNulEtVide)
{
	RTPRedundantPayload r1(NULL, 8);
	EXPECT_EQ(r1.GetRedundantCount(), 0);
	EXPECT_TRUE(r1.GetPrimaryPayloadData() == NULL);

	BYTE b = 0x00;
	RTPRedundantPayload r2(&b, 0);
	EXPECT_EQ(r2.GetRedundantCount(), 0);
	EXPECT_TRUE(r2.GetPrimaryPayloadData() == NULL);
}

// Bit F=1 (« un header suit ») mais buffer tronqué : RED incomplet -> rejeté.
TEST(Red, HeaderRedondantTronque)
{
	BYTE pkt[] = { 0xE4, 0x02, 0x80, 0x03 }; // annonce une suite, mais rien après
	RTPRedundantPayload red(pkt, sizeof(pkt));

	EXPECT_EQ(red.GetRedundantCount(), 0);
	EXPECT_TRUE(red.GetPrimaryPayloadData() == NULL);
	EXPECT_EQ(red.GetPrimaryPayloadSize(), 0u);
}

// Taille de redondance mensongère (1023 o annoncés, absents) : pas de sous-
// débordement de primarySize, payload primaire neutralisé.
TEST(Red, TailleRedondanceMensongere)
{
	// sz = 0x03FF = 1023 (b2 bits bas = 3, b3 = 0xFF), primaire F=0.
	BYTE pkt[] = { 0xE4, 0x02, 0x83, 0xFF, 0x62 };
	RTPRedundantPayload red(pkt, sizeof(pkt));

	EXPECT_TRUE(red.GetPrimaryPayloadData() == NULL);
	EXPECT_EQ(red.GetPrimaryPayloadSize(), 0u);
}
