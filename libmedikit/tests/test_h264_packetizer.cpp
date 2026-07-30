/**
 * test_h264_packetizer.cpp — packetisation RTP H.264 (VideoFrame::Packetize).
 *
 * Bug de production : la charge utile FU-A incluait l'octet d'en-tête NAL,
 * que le récepteur reconstruit déjà depuis l'indicateur et l'en-tête FU
 * (RFC 6184 §5.8). À la réception l'octet était donc doublé, toute la slice
 * décalée d'un octet : IDR indécodable dès le 3e macrobloc, sur tout NAL
 * fragmenté (> PKT_MTU). Les NAL courts, en single-NAL, étaient sains — d'où un
 * flux « presque » valide, très coûteux à diagnostiquer.
 *
 * Le test réassemble comme un récepteur RFC 6184 strict et compare au NAL
 * d'origine, octet à octet.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/video.h>
#include <h264/h264depacketizer.h>
#include <vector>

namespace {

const unsigned PKT_MTU = 1400;

// Construit une trame AVCC d'un seul NAL de `size` octets (en-tête compris).
void BuildAvccFrame(VideoFrame& vf, BYTE nalHeader, unsigned size)
{
	std::vector<BYTE> data(4 + size);
	data[0] = (size >> 24) & 0xFF;
	data[1] = (size >> 16) & 0xFF;
	data[2] = (size >> 8) & 0xFF;
	data[3] = size & 0xFF;
	data[4] = nalHeader;
	for (unsigned i = 1; i < size; i++) data[4 + i] = (BYTE)(i * 7 + 3);

	vf.SetMedia(&data[0], data.size());
	vf.SetH264NalSizeLength(4);
}

// Réassemblage RFC 6184 strict (comme l'UA) : single-NAL recopié tel quel,
// FU-A : en-tête reconstruit depuis indicateur+en-tête FU, charge SANS l'octet
// d'en-tête d'origine.
std::vector<BYTE> ReassembleRfc6184(VideoFrame& vf)
{
	std::vector<BYTE> out;
	MediaFrame::RtpPacketizationInfo& pi = vf.GetRtpPacketizationInfo();

	for (MediaFrame::RtpPacketizationInfo::iterator it = pi.begin(); it != pi.end(); ++it)
	{
		std::vector<BYTE> pl;
		if ((*it)->GetPrefixLen())
			pl.assign((*it)->GetPrefixData(), (*it)->GetPrefixData() + (*it)->GetPrefixLen());
		const BYTE* d = vf.GetData() + (*it)->GetPos();
		pl.insert(pl.end(), d, d + (*it)->GetSize());

		BYTE t = pl[0] & 0x1F;
		if (t == 28)
		{
			BYTE ind = pl[0], hdr = pl[1];
			if (hdr & 0x80)                              // S : reconstruire l'en-tête
				out.push_back((ind & 0xE0) | (hdr & 0x1F));
			out.insert(out.end(), pl.begin() + 2, pl.end());
		}
		else
		{
			out.insert(out.end(), pl.begin(), pl.end());
		}
	}
	return out;
}

} // namespace

// --- NAL court : single-NAL intact ------------------------------------------
TEST(H264Packetizer, SingleNalIntact)
{
	VideoFrame vf(VideoCodec::H264, 0x10000);
	BuildAvccFrame(vf, 0x41, 200);
	ASSERT_TRUE(vf.Packetize(PKT_MTU));

	std::vector<BYTE> out = ReassembleRfc6184(vf);
	ASSERT_EQ(out.size(), (size_t)200);
	EXPECT_EQ(0, memcmp(&out[0], vf.GetData() + 4, 200));
}

// --- NAL fragmenté : le réassemblage RFC 6184 doit rendre le NAL exact -------
TEST(H264Packetizer, FuaSansDuplicationDeLEnTete)
{
	const unsigned SIZES[] = { PKT_MTU + 1, 2 * PKT_MTU, 4239, 8600 };

	for (unsigned s = 0; s < sizeof(SIZES) / sizeof(SIZES[0]); s++)
	{
		VideoFrame vf(VideoCodec::H264, 0x10000);
		BuildAvccFrame(vf, 0x65, SIZES[s]);
		ASSERT_TRUE(vf.Packetize(PKT_MTU));

		// Tous les fragments doivent tenir dans le PKT_MTU + en-tête FU.
		// (un NAL de mtu+1 donne UN fragment FU portant S et E : charge = naluSz-1)
		MediaFrame::RtpPacketizationInfo& pi = vf.GetRtpPacketizationInfo();
		if (SIZES[s] > PKT_MTU + 1)
			ASSERT_GT(pi.size(), (size_t)1) << "NAL de " << SIZES[s] << " non fragmenté";
		for (MediaFrame::RtpPacketizationInfo::iterator it = pi.begin(); it != pi.end(); ++it)
			EXPECT_LE((*it)->GetSize() + (*it)->GetPrefixLen(), PKT_MTU + 2u);

		std::vector<BYTE> out = ReassembleRfc6184(vf);
		ASSERT_EQ(out.size(), (size_t)SIZES[s])
			<< "taille réassemblée fausse pour NAL de " << SIZES[s]
			<< " : en-tête NAL dupliqué ou perdu dans la charge FU-A";
		EXPECT_EQ(0, memcmp(&out[0], vf.GetData() + 4, SIZES[s]))
			<< "contenu réassemblé différent pour NAL de " << SIZES[s];

		// Le premier octet de charge du fragment S ne doit PAS être l'en-tête
		// NAL d'origine : il serait doublé chez le récepteur.
		MediaFrame::RtpPacketization* first = pi.front();
		ASSERT_EQ((int)(first->GetPrefixData()[0] & 0x1F), 28);
		EXPECT_NE((int)*(vf.GetData() + first->GetPos()), 0x65)
			<< "la charge FU-A commence par l'octet d'en-tête NAL";
	}
}

// --- Aller-retour avec NOTRE dépacketiseur -----------------------------------
// Le dépacketiseur maison reconstruit aussi l'en-tête (RFC 6184) : l'aller-
// retour Packetize -> H264Depacketizer doit rendre le NAL exact en AVCC.
TEST(H264Packetizer, AllerRetourAvecDepacketiseur)
{
	const unsigned SIZE = 4239;

	VideoFrame vf(VideoCodec::H264, 0x10000);
	BuildAvccFrame(vf, 0x65, SIZE);
	ASSERT_TRUE(vf.Packetize(PKT_MTU));

	H264Depacketizer depak;
	MediaFrame* mf = NULL;
	MediaFrame::RtpPacketizationInfo& pi = vf.GetRtpPacketizationInfo();
	std::vector<BYTE> pl;
	for (MediaFrame::RtpPacketizationInfo::iterator it = pi.begin(); it != pi.end(); ++it)
	{
		pl.clear();
		if ((*it)->GetPrefixLen())
			pl.assign((*it)->GetPrefixData(), (*it)->GetPrefixData() + (*it)->GetPrefixLen());
		const BYTE* d = vf.GetData() + (*it)->GetPos();
		pl.insert(pl.end(), d, d + (*it)->GetSize());
		mf = depak.AddPayload(&pl[0], pl.size(), (*it)->IsMark());
	}
	ASSERT_TRUE(mf != NULL);

	// AVCC : [len 4 octets][NAL] — identique à l'entrée.
	ASSERT_EQ(mf->GetLength(), vf.GetLength());
	EXPECT_EQ(0, memcmp(mf->GetData(), vf.GetData(), vf.GetLength()));
}
