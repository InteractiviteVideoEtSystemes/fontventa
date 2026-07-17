/**
 * test_utf8parser.cpp — UTF8Parser : calcul de taille, round-trip
 * wstring->UTF8->wstring, + cas adverses (séquences UTF-8 malformées, buffer de
 * sortie trop petit). Le parseur remplace les erreurs par '.' (jamais de crash).
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/text.h>
#include <string>
#include <vector>

// --- Nominal : GetUTF8Size par classe de code point -------------------------
TEST(Utf8Parser, TailleParClasse)
{
	EXPECT_EQ(UTF8Parser(std::wstring(L"Hello")).GetUTF8Size(), 5u); // ASCII 1o
	EXPECT_EQ(UTF8Parser(std::wstring(L"é")).GetUTF8Size(), 2u); // é   2o
	EXPECT_EQ(UTF8Parser(std::wstring(L"€")).GetUTF8Size(), 3u); // €   3o
	EXPECT_EQ(UTF8Parser(std::wstring(L"\U0001F600")).GetUTF8Size(), 4u); // 😀 4o
}

// --- Nominal : round-trip mixte 1/2/3/4 octets ------------------------------
TEST(Utf8Parser, RoundTripMixte)
{
	std::wstring src = L"Aé€\U0001F600"; // 1+2+3+4 = 10 octets
	UTF8Parser enc(src);
	ASSERT_EQ(enc.GetUTF8Size(), 10u);

	std::vector<BYTE> buf(enc.GetUTF8Size());
	DWORD n = enc.Serialize(buf.data(), buf.size());
	ASSERT_EQ(n, 10u);

	UTF8Parser dec;
	dec.SetSize(n);
	EXPECT_EQ(dec.Parse(buf.data(), n), n);
	EXPECT_TRUE(dec.IsParsed());
	EXPECT_EQ(dec.GetWString(), src);
}

// ============================================================================
// Cas adverses — entrées UTF-8 hostiles.
// ============================================================================

// Octet de continuation isolé (0x80) -> remplacé par '.'.
TEST(Utf8Parser, ContinuationIsoleeRemplacee)
{
	BYTE bad[] = { 0x80 };
	UTF8Parser p;
	p.SetSize(sizeof(bad));
	p.Parse(bad, sizeof(bad));
	EXPECT_EQ(p.GetWString(), std::wstring(L"."));
}

// Octet interdit (0xFF, hors de tout schéma) -> remplacé par '.'.
TEST(Utf8Parser, OctetInterditRemplace)
{
	BYTE bad[] = { 0xFF };
	UTF8Parser p;
	p.SetSize(sizeof(bad));
	p.Parse(bad, sizeof(bad));
	EXPECT_EQ(p.GetWString(), std::wstring(L"."));
}

// Début de séquence 2 octets suivi d'un ASCII (séquence interrompue) :
// le '.' d'erreur PUIS le caractère ASCII.
TEST(Utf8Parser, SequenceInterrompueParAscii)
{
	BYTE bad[] = { 0xC3, 0x41 }; // start 2o + 'A'
	UTF8Parser p;
	p.SetSize(sizeof(bad));
	p.Parse(bad, sizeof(bad));
	EXPECT_EQ(p.GetWString(), std::wstring(L".A"));
}

// Buffer de sortie trop petit -> Serialize renvoie 0 (rien écrit).
TEST(Utf8Parser, SerializeBufferTropPetit)
{
	UTF8Parser p(std::wstring(L"€")); // 3 octets requis
	BYTE small[2];
	EXPECT_EQ(p.Serialize(small, sizeof(small)), 0u);
}
