/**
 * test_amr_payload.cpp — charge utile RTP AMR / AMR-WB (RFC 4867 §4) <-> format
 * stockage (§5), dans les deux modes.
 *
 * Les vecteurs sont calculés à la main depuis les figures de la RFC (§4.3 pour
 * bandwidth-efficient, §4.4 pour octet-aligned). Les derniers tests passent par
 * les vrais codecs ffmpeg : c'est ce qui prouve que le format stockage est bien
 * celui que l'encodeur produit et que le décodeur attend.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/audio.h>
#include <medkit/codecs.h>
#include <amr/amrpayload.h>
#include <vector>
#include <cmath>
#include <memory>

namespace {

const bool WB = true;
const bool NB = false;
const bool OA = true;
const bool BE = false;

std::vector<BYTE> ToStorage(bool wideband, bool octetAligned, const std::vector<BYTE>& in)
{
	std::vector<BYTE> out(1500);
	int n = AMRPayload::ToStorage(wideband, octetAligned, in.data(), in.size(), out.data(), out.size());
	if (n < 0)
		return std::vector<BYTE>(1, 0xEE);
	out.resize(n);
	return out;
}

std::vector<BYTE> FromStorage(bool wideband, bool octetAligned, const std::vector<BYTE>& in)
{
	std::vector<BYTE> out(1500);
	int n = AMRPayload::FromStorage(wideband, octetAligned, in.data(), in.size(), out.data(), out.size());
	if (n < 0)
		return std::vector<BYTE>(1, 0xEE);
	out.resize(n);
	return out;
}

// Une trame au format stockage : en-tête 0|FT|Q|00 puis un corps dont seuls les
// `bits` premiers bits portent quelque chose, les derniers complétés de zéros.
std::vector<BYTE> StorageFrame(bool wideband, BYTE ft, BYTE seed)
{
	const int bits = AMRPayload::FrameBits(wideband, ft);
	std::vector<BYTE> f(1, (BYTE)((ft << 3) | 0x04));
	for (int i = 0; i < (bits + 7) / 8; i++)
		f.push_back((BYTE)(seed + 37 * i));
	if (bits % 8)
		f.back() &= (BYTE)(0xFF << (8 - bits % 8));
	return f;
}

} // namespace

TEST(AMRPayload, TaillesDesTrames)
{
	EXPECT_EQ(AMRPayload::FrameBits(WB, 0), 132);
	EXPECT_EQ(AMRPayload::FrameBits(WB, 8), 477);
	EXPECT_EQ(AMRPayload::FrameBits(WB, 9), 40);
	EXPECT_EQ(AMRPayload::FrameBits(WB, 14), 0);
	EXPECT_EQ(AMRPayload::FrameBits(WB, 15), 0);
	EXPECT_EQ(AMRPayload::FrameBits(WB, 10), -1);
	EXPECT_EQ(AMRPayload::FrameBits(NB, 7), 244);
	EXPECT_EQ(AMRPayload::FrameBits(NB, 8), 39);
	EXPECT_EQ(AMRPayload::FrameBits(NB, 9), -1);
	EXPECT_EQ(AMRPayload::FrameBits(NB, 15), 0);
}

// Octet-aligned : CMR 15, une entrée de table, puis le corps tel quel.
TEST(AMRPayload, OctetAlignedUneTrame)
{
	std::vector<BYTE> storage = StorageFrame(WB, 8, 1);
	ASSERT_EQ(storage.size(), 61u);

	std::vector<BYTE> rtp = FromStorage(WB, OA, storage);
	ASSERT_EQ(rtp.size(), 62u);
	EXPECT_EQ(rtp[0], 0xF0);
	EXPECT_EQ(rtp[1], 0x44); // F=0, FT=8, Q=1
	EXPECT_TRUE(std::equal(storage.begin() + 1, storage.end(), rtp.begin() + 2));

	EXPECT_EQ(ToStorage(WB, OA, rtp), storage);
}

// Bandwidth-efficient, NO_DATA : 1111 | 0 1111 1 | bourrage -> F7 C0.
TEST(AMRPayload, BandwidthEfficientSansParole)
{
	std::vector<BYTE> storage(1, 0x7C);
	std::vector<BYTE> rtp = FromStorage(WB, BE, storage);
	EXPECT_EQ(rtp, std::vector<BYTE>({ 0xF7, 0xC0 }));
	EXPECT_EQ(ToStorage(WB, BE, rtp), storage);
}

// Bandwidth-efficient, mode 0 (132 bits) tout à 1 : les bits de parole
// commencent au 11e bit, et 4+6+132 = 142 bits tiennent sur 18 octets.
TEST(AMRPayload, BandwidthEfficientDecaleLaParole)
{
	std::vector<BYTE> storage(1, 0x04);
	storage.insert(storage.end(), 16, 0xFF);
	storage.push_back(0xF0);

	std::vector<BYTE> rtp = FromStorage(WB, BE, storage);
	ASSERT_EQ(rtp.size(), 18u);
	EXPECT_EQ(rtp[0], 0xF0); // CMR 1111, F=0, FT 000…
	EXPECT_EQ(rtp[1], 0x7F); // …0, Q=1, puis six bits de parole
	for (int i = 2; i < 17; i++)
		EXPECT_EQ(rtp[i], 0xFF) << "octet " << i;
	EXPECT_EQ(rtp[17], 0xFC);

	EXPECT_EQ(ToStorage(WB, BE, rtp), storage);
}

// Plusieurs trames, de toutes tailles, dans les deux modes et les deux codecs :
// la table des matières chaînée (F=1 sauf la dernière) et l'ordre des corps.
TEST(AMRPayload, AllerRetourPlusieursTrames)
{
	struct Case { bool wideband; std::vector<BYTE> fts; };
	const Case cases[] = {
		{ WB, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 15 } },
		{ NB, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 15 } },
	};
	for (const Case& c : cases)
	{
		std::vector<BYTE> storage;
		for (size_t i = 0; i < c.fts.size(); i++)
		{
			std::vector<BYTE> f = StorageFrame(c.wideband, c.fts[i], (BYTE)(11 * i + 3));
			storage.insert(storage.end(), f.begin(), f.end());
		}
		for (bool mode : { OA, BE })
		{
			std::vector<BYTE> rtp = FromStorage(c.wideband, mode, storage);
			ASSERT_NE(rtp, std::vector<BYTE>(1, 0xEE));
			EXPECT_EQ(ToStorage(c.wideband, mode, rtp), storage)
				<< (c.wideband ? "WB " : "NB ") << (mode ? "octet-aligned" : "bandwidth-efficient");
		}
	}
}

// Adverse : rien de ce que le réseau envoie ne fait lire hors du paquet.
TEST(AMRPayload, ChargesInvalidesRefusees)
{
	std::vector<BYTE> rtp = FromStorage(WB, OA, StorageFrame(WB, 8, 1));
	rtp.pop_back();
	EXPECT_EQ(ToStorage(WB, OA, rtp), std::vector<BYTE>(1, 0xEE)); // corps tronqué

	EXPECT_EQ(ToStorage(WB, OA, { 0xF0, 0x84 }), std::vector<BYTE>(1, 0xEE)); // table sans fin
	EXPECT_EQ(ToStorage(WB, OA, { 0xF0, 0x54 }), std::vector<BYTE>(1, 0xEE)); // FT 10 réservé
	EXPECT_EQ(ToStorage(WB, BE, { 0xF0 }), std::vector<BYTE>(1, 0xEE));       // table tronquée
	EXPECT_EQ(ToStorage(WB, OA, { }), std::vector<BYTE>(1, 0xEE));
	EXPECT_EQ(ToStorage(NB, OA, { 0xF0, 0x4C }), std::vector<BYTE>(1, 0xEE)); // SID GSM-EFR

	EXPECT_EQ(FromStorage(WB, OA, { 0x44, 0x00 }), std::vector<BYTE>(1, 0xEE)); // corps tronqué
	EXPECT_EQ(FromStorage(WB, BE, { }), std::vector<BYTE>(1, 0xEE));

	BYTE small[4];
	std::vector<BYTE> storage = StorageFrame(WB, 8, 1);
	EXPECT_EQ(AMRPayload::FromStorage(WB, OA, storage.data(), storage.size(), small, sizeof(small)), -1);
	EXPECT_EQ(AMRPayload::FromStorage(WB, BE, storage.data(), storage.size(), small, sizeof(small)), -1);
}

// Le format stockage est celui des codecs ffmpeg : ce que l'encodeur produit,
// passé sur le fil et revenu, se décode. Deux trames dans un même paquet donnent
// deux trames décodées.
TEST(AMRPayload, LeFilEstLuParLeDecodeur)
{
	struct Case { AudioCodec::Type type; bool wideband; DWORD rate; DWORD frame; };
	const Case cases[] = {
		{ AudioCodec::AMRWB, WB, 16000, 320 },
		{ AudioCodec::AMR,   NB, 8000,  160 },
	};
	for (const Case& c : cases)
	{
		Properties props;
		std::unique_ptr<AudioEncoder> enc(AudioCodecFactory::CreateEncoder(c.type, props));
		std::unique_ptr<AudioDecoder> dec(AudioCodecFactory::CreateDecoder(c.type));
		if (!enc || !dec)
		{
			ADD_FAILURE() << AudioCodec::GetNameFor(c.type) << " absent de ffmpeg";
			continue;
		}

		std::vector<SWORD> pcm(c.frame * 2);
		for (size_t i = 0; i < pcm.size(); i++)
			pcm[i] = (SWORD)(8000.0 * sin(2.0 * M_PI * 440.0 * i / c.rate));

		std::vector<BYTE> storage;
		DWORD frames = 0;
		SamplesPtr in = Samples::FromBuffer(pcm.data(), pcm.size(), c.rate);
		for (AudioFramePtr f = enc->EncodeFrame(in); f; f = enc->EncodeFrame(NULL), frames++)
			storage.insert(storage.end(), f->GetData(), f->GetData() + f->GetLength());
		ASSERT_GE(frames, 2u) << AudioCodec::GetNameFor(c.type);

		for (bool mode : { OA, BE })
		{
			std::vector<BYTE> back = ToStorage(c.wideband, mode, FromStorage(c.wideband, mode, storage));
			ASSERT_EQ(back, storage);

			dec->Decode(back.data(), back.size());
			DWORD decoded = 0;
			for (SamplesPtr s = dec->GetFrame(); s; s = dec->GetFrame())
				decoded += s->GetNbSamples();
			EXPECT_EQ(decoded, frames * c.frame) << AudioCodec::GetNameFor(c.type);
		}
	}
}

// Le mode négocié se lit sur la trame elle-même : son en-tête porte le FT.
TEST(AMRPayload, LEncodeurEmetLeModeNegocie)
{
	struct Case { AudioCodec::Type type; const char* key; int mode; DWORD rate; DWORD frame; };
	const Case cases[] = {
		{ AudioCodec::AMRWB, "amrwb.mode", 2, 16000, 320 },
		{ AudioCodec::AMRWB, "amrwb.mode", 8, 16000, 320 },
		{ AudioCodec::AMR,   "amr.mode",   4, 8000,  160 },
	};
	for (const Case& c : cases)
	{
		Properties props;
		props[c.key] = std::to_string(c.mode);
		std::unique_ptr<AudioEncoder> enc(AudioCodecFactory::CreateEncoder(c.type, props));
		ASSERT_TRUE(enc != nullptr);

		std::vector<SWORD> pcm(c.frame * 3);
		for (size_t i = 0; i < pcm.size(); i++)
			pcm[i] = (SWORD)(8000.0 * sin(2.0 * M_PI * 440.0 * i / c.rate));
		SamplesPtr in = Samples::FromBuffer(pcm.data(), pcm.size(), c.rate);

		int frames = 0;
		for (AudioFramePtr f = enc->EncodeFrame(in); f; f = enc->EncodeFrame(NULL), frames++)
			EXPECT_EQ((f->GetData()[0] >> 3) & 0x0F, c.mode) << c.key << "=" << c.mode;
		EXPECT_GT(frames, 0);
	}
}

TEST(AMRPayload, NegociationDuFmtp)
{
	std::string fmtp;
	int mode;

	// Rien de dit : bandwidth-efficient, tous les modes.
	ASSERT_TRUE(AMRPayload::Negotiate(WB, {}, fmtp, mode));
	EXPECT_EQ(fmtp, "");
	EXPECT_EQ(mode, 8);
	EXPECT_FALSE(AMRPayload::OctetAligned({}));

	// octet-align se reflète, le mode-set est repris et borne l'encodeur.
	ASSERT_TRUE(AMRPayload::Negotiate(WB, { { "octet-align", "1" }, { "mode-set", "0,1,2" } }, fmtp, mode));
	EXPECT_EQ(fmtp, "octet-align=1;mode-set=0,1,2");
	EXPECT_EQ(mode, 2);
	EXPECT_TRUE(AMRPayload::OctetAligned({ { "octet-align", "1" } }));
	EXPECT_FALSE(AMRPayload::OctetAligned({ { "octet-align", "0" } }));

	// Un mode hors bornes est ignoré ; un mode-set qui n'en garde aucun refuse le PT.
	ASSERT_TRUE(AMRPayload::Negotiate(NB, { { "mode-set", "7,9" } }, fmtp, mode));
	EXPECT_EQ(fmtp, "mode-set=7");
	EXPECT_EQ(mode, 7);
	EXPECT_FALSE(AMRPayload::Negotiate(WB, { { "mode-set", "9" } }, fmtp, mode));

	// Ce que nous ne portons pas.
	EXPECT_FALSE(AMRPayload::Negotiate(WB, { { "crc", "1" } }, fmtp, mode));
	EXPECT_FALSE(AMRPayload::Negotiate(WB, { { "robust-sorting", "1" } }, fmtp, mode));
	EXPECT_FALSE(AMRPayload::Negotiate(WB, { { "interleaving", "4" } }, fmtp, mode));
	EXPECT_TRUE(AMRPayload::Negotiate(WB, { { "crc", "0" } }, fmtp, mode));
}

// Un pair en DTX alterne parole, trame SID (descripteur de bruit de fond) et
// trames NO_DATA. Le décodeur natif d'ffmpeg rejette les SID, et en AMR-NB aussi
// les NO_DATA : chaque trame rejetée était 20 ms de moins dans le flux décodé.
TEST(AMRPayload, LeDecodeurNePerdAucuneTrameDTX)
{
	struct Case { AudioCodec::Type type; BYTE sid; DWORD sidBytes; DWORD rate; DWORD frame; };
	const Case cases[] = {
		{ AudioCodec::AMRWB, 0x4C, 5, 16000, 320 },	// FT 9, 40 bits
		{ AudioCodec::AMR,   0x44, 5, 8000,  160 },	// FT 8, 39 bits
	};
	for (const Case& c : cases)
	{
		Properties props;
		std::unique_ptr<AudioEncoder> enc(AudioCodecFactory::CreateEncoder(c.type, props));
		std::unique_ptr<AudioDecoder> dec(AudioCodecFactory::CreateDecoder(c.type));
		ASSERT_TRUE(enc && dec) << AudioCodec::GetNameFor(c.type);

		std::vector<SWORD> pcm(c.frame * 10);
		for (size_t i = 0; i < pcm.size(); i++)
			pcm[i] = (SWORD)(8000.0 * sin(2.0 * M_PI * 440.0 * i / c.rate));
		std::vector<std::vector<BYTE>> speech;
		SamplesPtr in = Samples::FromBuffer(pcm.data(), pcm.size(), c.rate);
		for (AudioFramePtr f = enc->EncodeFrame(in); f; f = enc->EncodeFrame(NULL))
			speech.push_back(std::vector<BYTE>(f->GetData(), f->GetData() + f->GetLength()));
		ASSERT_GE(speech.size(), 4u);

		std::vector<std::vector<BYTE>> stream(speech.begin(), speech.begin() + 2);
		for (int period = 0; period < 2; period++)
		{
			std::vector<BYTE> sid(1, c.sid);
			sid.resize(1 + c.sidBytes, 0);
			stream.push_back(sid);
			for (int i = 0; i < 7; i++)
				stream.push_back(std::vector<BYTE>(1, 0x7C));
		}
		stream.insert(stream.end(), speech.begin() + 2, speech.end());

		DWORD decoded = 0;
		for (std::vector<BYTE> frame : stream)
		{
			dec->Decode(frame.data(), frame.size());
			for (SamplesPtr s = dec->GetFrame(); s; s = dec->GetFrame())
				decoded += s->GetNbSamples();
		}
		EXPECT_EQ(decoded, stream.size() * c.frame) << AudioCodec::GetNameFor(c.type);
	}
}
