/**
 * test_avcdescriptor.cpp — AVCDescriptor (avcC) : round-trip Serialize/Parse +
 * cas adverses (buffer tronqué, longueur SPS/PPS mensongère, buffer de sortie
 * trop petit). Parse() est déjà borné ; les tests le prouvent.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/avcdescriptor.h>
#include <cstring>
#include <vector>

namespace {
// SPS iOS + un PPS court arbitraire.
BYTE kSps[] = { 0x42, 0x00, 0x1e, 0xab, 0x40, 0x50, 0x1e, 0xc8 };
BYTE kPps[] = { 0xce, 0x38, 0x80 };
}

// --- Nominal : round-trip complet -------------------------------------------
TEST(AvcDescriptor, RoundTrip)
{
	AVCDescriptor src;
	src.SetConfigurationVersion(1);
	src.SetAVCProfileIndication(66);
	src.SetProfileCompatibility(0xC0);
	src.SetAVCLevelIndication(30);
	src.SetNALUnitLength(3);
	src.AddSequenceParameterSet(kSps, sizeof(kSps));
	src.AddPictureParameterSet(kPps, sizeof(kPps));

	std::vector<BYTE> buf(src.GetSize());
	DWORD written = src.Serialize(buf.data(), buf.size());
	ASSERT_EQ(written, src.GetSize());

	AVCDescriptor dst;
	ASSERT_TRUE(dst.Parse(buf.data(), written));

	EXPECT_EQ(dst.GetConfigurationVersion(), 1);
	EXPECT_EQ(dst.GetAVCProfileIndication(), 66);
	EXPECT_EQ(dst.GetProfileCompatibility(), 0xC0);
	EXPECT_EQ(dst.GetAVCLevelIndication(), 30);
	EXPECT_EQ(dst.GetNALUnitLength(), 3);        // seuls les 2 bits bas transitent
	ASSERT_EQ(dst.GetNumOfSequenceParameterSets(), 1);
	ASSERT_EQ(dst.GetNumOfPictureParameterSets(), 1);
	ASSERT_EQ(dst.GetSequenceParameterSetSize(0), sizeof(kSps));
	EXPECT_EQ(0, memcmp(dst.GetSequenceParameterSet(0), kSps, sizeof(kSps)));
	ASSERT_EQ(dst.GetPictureParameterSetSize(0), sizeof(kPps));
	EXPECT_EQ(0, memcmp(dst.GetPictureParameterSet(0), kPps, sizeof(kPps)));
}

// ============================================================================
// Cas adverses — Parse() doit refuser proprement (déjà borné).
// ============================================================================

// Buffer sous la taille minimale de l'en-tête (7 octets).
TEST(AvcDescriptor, RejetteEnteteTropCourt)
{
	BYTE buf[6] = { 1, 66, 0xC0, 30, 0xFC, 0xE0 };
	AVCDescriptor dst;
	EXPECT_FALSE(dst.Parse(buf, sizeof(buf)));
}

// Un SPS annoncé (num=1) mais pas la place pour son champ de longueur.
TEST(AvcDescriptor, RejetteSpsTronque)
{
	// version, prof, compat, level, nalu, numSPS(0xE1 -> 1), puis 1 seul octet.
	BYTE buf[] = { 1, 66, 0xC0, 30, 0xFC, 0xE1, 0x00 };
	AVCDescriptor dst;
	EXPECT_FALSE(dst.Parse(buf, sizeof(buf)));
}

// Longueur SPS mensongère : le champ annonce 16 octets absents du buffer.
TEST(AvcDescriptor, RejetteLongueurSpsMensongere)
{
	BYTE buf[] = { 1, 66, 0xC0, 30, 0xFC, 0xE1, 0x00, 0x10 /* len=16 */ };
	AVCDescriptor dst;
	EXPECT_FALSE(dst.Parse(buf, sizeof(buf)));
}

// Buffer de sortie trop petit -> Serialize renvoie (DWORD)-1.
TEST(AvcDescriptor, SerializeBufferTropPetit)
{
	AVCDescriptor src;
	src.AddSequenceParameterSet(kSps, sizeof(kSps));
	BYTE small[2];
	EXPECT_EQ(src.Serialize(small, sizeof(small)), (DWORD)-1);
}
