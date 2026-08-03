/**
 * test_h264_sps.cpp — portage gtest du harnais testsps.cpp.
 *
 * Décode un SPS H264 réel (capturé sur un endpoint iOS) et vérifie les champs
 * dérivés (profil, niveau, dimensions) au lieu du simple Dump() d'origine.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>   // h264.h::Dump() utilise Debug()/Log() -> à inclure avant
#include <h264/h264.h>

// SPS iOS : profile_idc=0x42 (Baseline 66), level_idc=0x1e (30),
// pic_width_in_mbs_minus1=39 -> 640, pic_height_in_map_units_minus1=29 -> 480.
TEST(H264Sps, DecodeIosBaseline)
{
	BYTE sps[] = { 0x42, 0x00, 0x1e, 0xab, 0x40, 0x50, 0x1e, 0xc8 };
	H264SeqParameterSet decoded;

	ASSERT_TRUE(decoded.Decode(sps, sizeof(sps)));
	EXPECT_EQ(decoded.GetProfile(), 66);   // Baseline
	EXPECT_EQ(decoded.GetLevel(),   30);   // 3.0
	EXPECT_EQ(decoded.GetWidth(),   640u);
	EXPECT_EQ(decoded.GetHeight(),  480u);
}

// ============================================================================
// Cas d'erreur — Decode() a été durci (h264.h) pour DÉTECTER les entrées
// invalides au lieu de retourner true systématiquement.
// ============================================================================

// Buffer nul -> refusé.
TEST(H264Sps, RejetteBufferNul)
{
	H264SeqParameterSet sps;
	EXPECT_FALSE(sps.Decode(NULL, 8));
}

// Taille nulle -> refusée (sous le plancher des 3 octets fixes).
TEST(H264Sps, RejetteTailleNulle)
{
	BYTE buf[] = { 0x42, 0x00, 0x1e, 0xab };
	H264SeqParameterSet sps;
	EXPECT_FALSE(sps.Decode(buf, 0));
}

// Taille sous le plancher (< 4 octets) -> refusée : impossible de porter
// profile_idc + contraintes + level_idc + le moindre exp-Golomb.
TEST(H264Sps, RejetteTropCourt)
{
	BYTE buf[] = { 0x42, 0x00, 0x1e };
	H264SeqParameterSet sps;
	EXPECT_FALSE(sps.Decode(buf, 3));
}
