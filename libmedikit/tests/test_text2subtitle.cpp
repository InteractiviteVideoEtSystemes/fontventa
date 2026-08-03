/**
 * test_text2subtitle.cpp — Text2Subtitle : accumulation de texte temps réel
 * (RTT), fins de ligne, effacements, + cas adverses (effacement sur ligne/
 * historique vides, sur-effacement, CRLF). Doit rester stable (pas d'underflow).
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/text2subtitle.h>
#include <string>

// --- Nominal : accumulation simple ------------------------------------------
TEST(Text2Subtitle, AccumuleLigne)
{
	Text2Subtitle t;
	EXPECT_EQ(t.Accumulate(L"Hello"), 1); // 1 = accumulé sans fin de ligne

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "Hello");
}

// --- Nominal : fin de ligne pousse dans l'historique ------------------------
TEST(Text2Subtitle, FinDeLignePousseHistorique)
{
	Text2Subtitle t;
	EXPECT_EQ(t.Accumulate(L"Hi\n"), 2); // 2 = ligne poussée

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "");                   // la ligne courante est vidée

	std::string sub;
	t.GetSubtitle(sub);
	EXPECT_EQ(sub, "Hi\n");               // la ligne est dans le sous-titre
}

// --- Nominal : backspace efface le dernier caractère ------------------------
TEST(Text2Subtitle, BackspaceEffaceDernier)
{
	Text2Subtitle t;
	t.Accumulate(L"AB\x08");              // 0x08 = backspace

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "A");
}

// --- Nominal : BOM (U+FEFF) ignoré ------------------------------------------
TEST(Text2Subtitle, BomIgnore)
{
	Text2Subtitle t;
	t.Accumulate(L"A﻿B");

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "AB");
}

// --- Nominal : chaîne vide -> aucun traitement ------------------------------
TEST(Text2Subtitle, ChaineVide)
{
	Text2Subtitle t;
	EXPECT_EQ(t.Accumulate(L""), 0);
}

// ============================================================================
// Cas adverses — entrées d'édition hostiles. Ne doivent pas déborder.
// ============================================================================

// Backspace alors que ligne ET historique sont vides -> stable.
TEST(Text2Subtitle, BackspaceSurVide)
{
	Text2Subtitle t;
	t.Accumulate(L"\x08");               // ne doit pas crasher

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "");
}

// Plus de backspaces que de caractères -> pas d'underflow, ligne vide.
TEST(Text2Subtitle, SurEffacement)
{
	Text2Subtitle t;
	t.Accumulate(L"A\x08\x08\x08");

	std::string cur;
	t.GetCurrentLine(cur);
	EXPECT_EQ(cur, "");
}

// CRLF : le \n suivant un \r ne crée pas une deuxième ligne.
TEST(Text2Subtitle, CrLfUneSeuleLigne)
{
	Text2Subtitle t;
	EXPECT_EQ(t.Accumulate(L"X\r\nY"), 2);

	std::string sub;
	t.GetSubtitle(sub);
	EXPECT_EQ(sub, "X\nY");              // "X\n" (historique) + "Y" (courante)
}
