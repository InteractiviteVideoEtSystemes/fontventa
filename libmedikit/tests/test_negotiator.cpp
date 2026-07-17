/**
 * test_negotiator.cpp — portage gtest du harnais negotest.cpp.
 *
 * Vérifie le contrat du retour enrichi d'EndpointStartReceiving (§5.2 de
 * nego_fmtp.md / §6.7 de xmlrpc_jsr309_api.md) : la struct fmtpByPt renvoyée au
 * contrôleur SIP. On reconstruit la map EXACTEMENT comme
 * Endpoint::Port::NegotiateReceiving (tout PT retenu est présent, même fmtp
 * vide) pour tester la sémantique bout-en-bout :
 *   - présence de la clé = PT accepté ; absence = PT filtré (non supporté) ;
 *   - codec sans fmtp -> valeur "" ; codec avec fmtp -> chaîne (params seuls) ;
 *   - telephone-event -> plage de tonalités ; T140RED -> liste de redondance.
 */
#include <gtest/gtest.h>
#include "medkit/negotiator.h"
#include <string>
#include <map>

namespace {

// Reconstruit la struct fmtpByPt comme Endpoint::Port::NegotiateReceiving :
// TOUT PT retenu par le négociateur est présent, y compris fmtp vide.
std::map<int,std::string> BuildFmtpByPt(MediaFrame::Type media,
                                        const std::map<int,int>& proposed,
                                        const Properties& localProps)
{
	NegotiationResult result;
	std::map<int,std::string> out;
	if (!CodecNegotiator::Negotiate(media, proposed, localProps, NULL, result))
		return out;
	for (size_t i = 0; i < result.codecs.size(); i++)
		out[result.codecs[i].payloadType] = result.codecs[i].fmtp;
	return out;
}

} // namespace

// --- Cas 1 : audio mixte (PCMU sans fmtp + telephone-event + PT non supporté) -
TEST(Negotiator, AudioMixte)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[0]   = AudioCodec::PCMU;            // 0
	proposed[101] = AudioCodec::TELEPHONE_EVENT; // 100
	proposed[200] = 200;                         // inconnu -> non supporté
	std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Audio, proposed, props);

	ASSERT_TRUE(m.count(0));                     // PCMU présent
	EXPECT_EQ(m[0], "");                         // ... avec fmtp vide
	ASSERT_TRUE(m.count(101));                   // telephone-event présent
	EXPECT_EQ(m[101], "0-16");                   // ... plage de tonalités
	EXPECT_EQ(m.count(200), 0u);                 // codec inconnu filtré
}

// --- Cas 2 : vidéo mixte (H264 avec fmtp + ULPFEC filtré) --------------------
TEST(Negotiator, VideoMixte)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[96]  = VideoCodec::H264;   // 96
	proposed[108] = VideoCodec::ULPFEC; // 108 -> non supporté
	std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Video, proposed, props);

	ASSERT_TRUE(m.count(96));
	EXPECT_EQ(m[96], "profile-level-id=42801f;packetization-mode=1");
	EXPECT_EQ(m.count(108), 0u);                 // ULPFEC filtré
}

// --- Cas 3 : texte T140 + T140RED (liste de redondance cohérente) -----------
TEST(Negotiator, TexteT140Red)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[105] = TextCodec::T140RED; // 105
	proposed[106] = TextCodec::T140;    // 106
	std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Text, proposed, props);

	ASSERT_TRUE(m.count(106));
	EXPECT_EQ(m[106], "");                       // T140 sans fmtp
	ASSERT_TRUE(m.count(105));
	EXPECT_EQ(m[105], "106/106/106");            // T140RED réf le PT T140 accepté
}

// --- Cas 4 : non-régression — rien d'accepté n'est perdu (tous sans fmtp) ----
TEST(Negotiator, TroisCodecsSansFmtp)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[0] = AudioCodec::PCMU; // 0
	proposed[8] = AudioCodec::PCMA; // 8
	proposed[9] = AudioCodec::G722; // 9
	std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Audio, proposed, props);

	ASSERT_EQ(m.size(), 3u);
	EXPECT_TRUE(m.count(0) && m.count(8) && m.count(9));
	EXPECT_TRUE(m[0].empty() && m[8].empty() && m[9].empty());
}

// ============================================================================
// Cas d'erreur / limites — vérifient la DÉTECTION par le négociateur.
// ============================================================================

// Média non négociable (Application) -> Negotiate retourne false ET vide out,
// même si celui-ci contenait des résultats d'un appel précédent (garde-fou du
// clear en début de Negotiate).
TEST(Negotiator, MediaNonNegociableRejete)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[0] = AudioCodec::PCMU;

	// out pré-rempli pour vérifier qu'il est bien réinitialisé.
	NegotiationResult out;
	out.acceptedMap[42] = 42;
	out.codecs.push_back(NegotiatedCodec());

	EXPECT_FALSE(CodecNegotiator::Negotiate(MediaFrame::Application, proposed,
	                                        props, NULL, out));
	EXPECT_TRUE(out.acceptedMap.empty());
	EXPECT_TRUE(out.codecs.empty());
}

// Map proposée vide -> succès (média négociable) mais résultat vide.
TEST(Negotiator, MapProposeeVide)
{
	Properties props;
	std::map<int,int> proposed; // vide
	NegotiationResult out;

	EXPECT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Audio, proposed,
	                                       props, NULL, out));
	EXPECT_TRUE(out.acceptedMap.empty());
	EXPECT_TRUE(out.codecs.empty());
}

// Aucun PT proposé n'est supporté -> succès mais résultat vide (tout filtré).
TEST(Negotiator, AucunCodecSupporte)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[200] = 200; // inconnu
	proposed[201] = 201; // inconnu
	NegotiationResult out;

	EXPECT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Audio, proposed,
	                                       props, NULL, out));
	EXPECT_TRUE(out.acceptedMap.empty());
	EXPECT_TRUE(out.codecs.empty());
}

// T140RED offert SANS T140 companion -> accepté mais fmtp vide (pas de PT
// primaire à référencer). Le RED orphelin ne doit pas produire "xx/xx/xx".
TEST(Negotiator, T140RedOrphelin)
{
	Properties props;
	std::map<int,int> proposed;
	proposed[105] = TextCodec::T140RED; // 105, sans 106
	std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Text, proposed, props);

	ASSERT_TRUE(m.count(105));
	EXPECT_EQ(m[105], "");
}
