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
#include "h264/h264encoder.h"   // WantsHardware / WantedPacketizationMode (mode 0 -> logiciel)
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
	// level-asymmetry-allowed=1 est annoncé depuis la phase 5 : sans lui,
	// RFC 6184 §8.2.2 nous imposerait le niveau de l'offre même quand nous savons
	// décoder mieux (l'asymétrie exige le paramètre des DEUX côtés).
	EXPECT_EQ(m[96], "profile-level-id=42801f;packetization-mode=1;level-asymmetry-allowed=1");
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

// ---------------------------------------------------------------------------
// Ingestion du fmtp distant — H.264 (phase 5, RFC 6184 §8.2.2)
//
// Deux choses distinctes sortent de la négociation et ne doivent pas être
// confondues : ce qu'on ANNONCE (notre capacité de réception) et ce qui BORNE
// NOTRE ENCODEUR (ce que le pair a déclaré savoir décoder). Ces tests pinnent
// les deux, plus l'écart assumé à la RFC quand le niveau offert nous dépasse.
// ---------------------------------------------------------------------------

namespace {

// Le fmtp distant voyage sous "<nomcodec>.fmtp" dans les mêmes Properties que
// notre config (convention §5.3, cf. Endpoint::Port::NegotiateReceiving). C'est la
// clé historique, celle du JSR-309 : un PT par codec.
Properties H264Props(const std::string& localPlid, const std::string& remoteFmtp)
{
	Properties p;
	p["h264.profile-level-id"] = localPlid;
	if (!remoteFmtp.empty())
		p["h264.fmtp"] = remoteFmtp;
	return p;
}

// La clé par PAYLOAD TYPE, celle du MCU (paramètre `offer` de StartReceiving) : un
// même codec sous plusieurs PT, chacun avec son propre fmtp.
Properties H264PtProps(const std::string& localPlid,
                       const std::map<int,std::string>& remoteByPt)
{
	Properties p;
	p["h264.profile-level-id"] = localPlid;
	for (std::map<int,std::string>::const_iterator it=remoteByPt.begin(); it!=remoteByPt.end(); ++it)
	{
		char key[32];
		snprintf(key,sizeof(key),"pt.%d.fmtp",it->first);
		p[key] = it->second;
	}
	return p;
}

// Le fmtp annoncé pour un PT donné dans un résultat de négociation.
std::string FmtpOf(const NegotiationResult& out, int pt)
{
	for (size_t i=0;i<out.codecs.size();i++)
		if (out.codecs[i].payloadType == pt)
			return out.codecs[i].fmtp;
	return "(pt absent)";
}

// Négocie un unique PT H.264 et rend le couple (fmtp annoncé, props encodeur).
void NegotiateH264(const Properties& props, std::string& announced,
                   std::string& effectivePlid)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed,
	                                       props, &props, out));
	ASSERT_EQ(out.codecs.size(), 1u);
	announced     = out.codecs[0].fmtp;
	effectivePlid = out.codecs[0].effectiveProps.GetProperty(
	                    "h264.profile-level-id", std::string());
}

} // namespace

// Le parseur de paramètres fmtp : casse, espaces, drapeau nu, chaîne vide.
TEST(FmtpParams, Parse)
{
	std::map<std::string,std::string> m =
		ParseFmtpParams("Profile-Level-Id=42E01F; packetization-mode=1 ;flag");

	EXPECT_EQ(m["profile-level-id"], "42E01F"); // clé normalisée, valeur intacte
	EXPECT_EQ(m["packetization-mode"], "1");
	ASSERT_TRUE(m.count("flag"));
	EXPECT_EQ(m["flag"], "");
	EXPECT_TRUE(ParseFmtpParams("").empty());
}

// Pas de fmtp distant (offer sortant, ou pair muet) : on annonce notre config,
// et l'encodeur n'est borné que par elle.
TEST(NegotiatorH264, SansFmtpDistant)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", ""), announced, effective);

	EXPECT_NE(announced.find("profile-level-id=42801f"), std::string::npos);
	EXPECT_NE(announced.find("level-asymmetry-allowed=1"), std::string::npos);
	EXPECT_EQ(effective, "42801F"); // notre config, inchangée
}

// Pas d'asymétrie (paramètre absent) et niveau décodable : règles 1+2 combinées
// -> on renvoie le profile-level-id de l'offre TEL QUEL. C'est le cas courant
// des postes SIP, et le test de non-régression du comportement historique.
TEST(NegotiatorH264, SansAsymetrieRefletExact)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", "profile-level-id=42e01e;packetization-mode=1"),
	              announced, effective);

	// 0x1e = niveau 3.0, sous notre 0x1f : on le reflète, profil de l'offre inclus.
	EXPECT_NE(announced.find("profile-level-id=42e01e"), std::string::npos);
	// L'encodeur est borné par le minimum, ici celui du pair.
	EXPECT_EQ(effective, "42e01e");
}

// level-asymmetry-allowed=1 chez le pair : on annonce NOTRE niveau (capacité de
// décodage réelle), tout en gardant le profil de l'offre.
TEST(NegotiatorH264, AsymetriePermise)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", "profile-level-id=42e01e;level-asymmetry-allowed=1"),
	              announced, effective);

	// Profil de l'offre (42e0) + NOTRE niveau (1f).
	EXPECT_NE(announced.find("profile-level-id=42e01f"), std::string::npos);
	// L'encodeur reste borné par ce que le pair sait décoder (1e).
	EXPECT_EQ(effective, "42e01e");
}

// Niveau offert au-dessus de notre capacité, sans asymétrie : écart assumé à la
// RFC — on annonce NOTRE maximum et on garde le PT (refuser la vidéo est un
// échec plus dur qu'annoncer en dessous).
TEST(NegotiatorH264, NiveauOffertNonDecodable)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", "profile-level-id=42e028"), // 0x28 = niveau 4.0
	              announced, effective);

	EXPECT_NE(announced.find("profile-level-id=42e01f"), std::string::npos);
	// Et l'encodeur ne dépasse pas notre propre capacité.
	EXPECT_EQ(effective, "42e01f");
}

// fmtp distant illisible : on ne devine pas, on garde notre config, et le PT
// reste accepté (un fmtp mal formé ne doit pas coûter la vidéo).
TEST(NegotiatorH264, FmtpDistantIllisible)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", "profile-level-id=nawak"), announced, effective);

	EXPECT_NE(announced.find("profile-level-id=42801f"), std::string::npos);
	EXPECT_EQ(effective, "42801F");
}

// packetization-mode : celui du PAIR, dans les deux jeux. Il fait partie de
// l'identité du payload type côté pair — un PT offert en mode 0 et répondu en mode 1
// n'est pas le codec qu'il a proposé, et un navigateur refuse la réponse entière.
// Ce test pinnait l'inverse ("on annonce le nôtre"), corrigé le 2026-08-06.
TEST(NegotiatorH264, PacketizationModeDistant)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;
	Properties props = H264Props("42801F", "profile-level-id=42e01f;packetization-mode=0");

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 1u);

	EXPECT_NE(out.codecs[0].fmtp.find("packetization-mode=0"), std::string::npos);
	EXPECT_EQ(out.codecs[0].effectiveProps.GetProperty("h264.packetization-mode",
	                                                   std::string()), "0");
}

// Sans entrée distante, le mode annoncé reste le nôtre (offer sortant, pair muet).
TEST(NegotiatorH264, PacketizationModeParDefaut)
{
	std::string announced, effective;
	NegotiateH264(H264Props("42801F", ""), announced, effective);

	EXPECT_NE(announced.find("packetization-mode=1"), std::string::npos);
}

// Absence de packetization-mode = pas de contrainte = 1, dans les deux jeux (écart
// assumé à la RFC 6184 §8.1, décidé le 2026-08-06). L'encodeur doit lire une valeur
// explicite plutôt que redeviner un défaut.
TEST(NegotiatorH264, ModeAbsentVautUnDansLesDeuxJeux)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;
	Properties props = H264Props("42801F", "profile-level-id=42801f");

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 1u);

	EXPECT_NE(out.codecs[0].fmtp.find("packetization-mode=1"), std::string::npos);
	EXPECT_EQ(out.codecs[0].effectiveProps.GetProperty("h264.packetization-mode",
	                                                   std::string()), "1");
}

// Mode 0 explicite : annoncé 0 et borné 0. C'est le seul cas qui fait basculer
// l'encodeur en logiciel (H264Encoder::WantsHardware).
TEST(NegotiatorH264, ModeZeroExpliciteBorneLesDeuxJeux)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;
	Properties props = H264Props("42801F", "profile-level-id=42801f;packetization-mode=0");

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 1u);

	EXPECT_NE(out.codecs[0].fmtp.find("packetization-mode=0"), std::string::npos);
	EXPECT_EQ(out.codecs[0].effectiveProps.GetProperty("h264.packetization-mode",
	                                                   std::string()), "0");
}

// Le mode demandé décide de l'encodeur : 0 interdit le FU-A, donc interdit VAAPI, qui
// ne sait pas borner la taille d'une slice.
TEST(H264Encoder, ModeZeroInterditLAccelerationMaterielle)
{
	Properties none;
	EXPECT_TRUE(H264Encoder::WantsHardware(none));
	EXPECT_EQ(H264Encoder::WantedPacketizationMode(none), 1);

	Properties mode1;
	mode1["h264.packetization-mode"] = "1";
	EXPECT_TRUE(H264Encoder::WantsHardware(mode1));

	Properties mode0;
	mode0["h264.packetization-mode"] = "0";
	EXPECT_FALSE(H264Encoder::WantsHardware(mode0));
	EXPECT_EQ(H264Encoder::WantedPacketizationMode(mode0), 0);

	// hwaccel exigé ET mode 0 : deux demandes incompatibles, le matériel gagne et
	// l'incohérence est journalisée (le pair ne décodera pas).
	Properties conflict;
	conflict["h264.packetization-mode"] = "0";
	conflict["video.hwaccel.required"]  = "1";
	EXPECT_TRUE(H264Encoder::WantsHardware(conflict));
}

// LE cas de la capture du 2026-08-06 : un navigateur offre le même H.264 sous sept
// payload types — quatre profils × deux modes de paquetisation — et chacun doit
// repartir avec SON fmtp. La clé par nom de codec ne pouvait porter qu'une
// résolution pour les sept ; la clé par PT en porte sept.
TEST(NegotiatorH264, SeptPayloadTypesChacunSonProfil)
{
	std::map<int,int> proposed;
	std::map<int,std::string> remote;
	// (pt, profil, mode) tels que Chrome 138 les offre
	remote[39]  = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f";
	remote[103] = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f";
	remote[107] = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f";
	remote[109] = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f";
	remote[115] = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f";
	remote[117] = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f";
	remote[119] = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f";
	for (std::map<int,std::string>::const_iterator it=remote.begin(); it!=remote.end(); ++it)
		proposed[it->first] = VideoCodec::H264;

	// notre capacité : niveau 1f, et le pair autorise l'asymétrie partout, donc le
	// niveau annoncé est le nôtre et le PROFIL est celui de chaque PT
	Properties props = H264PtProps("42801F", remote);

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 7u);

	EXPECT_NE(FmtpOf(out,39).find("profile-level-id=4d001f"),  std::string::npos);
	EXPECT_NE(FmtpOf(out,39).find("packetization-mode=0"),     std::string::npos);
	EXPECT_NE(FmtpOf(out,103).find("profile-level-id=42001f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,103).find("packetization-mode=1"),    std::string::npos);
	EXPECT_NE(FmtpOf(out,107).find("profile-level-id=42001f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,107).find("packetization-mode=0"),    std::string::npos);
	EXPECT_NE(FmtpOf(out,109).find("profile-level-id=42e01f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,115).find("profile-level-id=42e01f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,117).find("profile-level-id=4d001f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,119).find("profile-level-id=64001f"), std::string::npos);

	// et surtout : plus aucun PT ne porte le profil d'un autre. Avant le correctif,
	// les sept portaient 64001f (le dernier PT itéré gagnait).
	int with64 = 0;
	for (size_t i=0;i<out.codecs.size();i++)
		if (out.codecs[i].fmtp.find("profile-level-id=64001f") != std::string::npos)
			with64++;
	EXPECT_EQ(with64, 1);
}

// La clé par PT gagne sur la clé par nom de codec quand les deux sont là : un
// contrôleur qui migre peut envoyer les deux sans que l'ancienne écrase la nouvelle.
TEST(NegotiatorH264, ClePtPrioritaireSurCleCodec)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;

	std::map<int,std::string> remote;
	remote[96] = "profile-level-id=42e01f";
	Properties props = H264PtProps("42801F", remote);
	props["h264.fmtp"] = "profile-level-id=64001f";

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 1u);
	EXPECT_NE(out.codecs[0].fmtp.find("profile-level-id=42e01f"), std::string::npos);
}

// Un PT sans entrée distante (l'offre n'a pas écrit de fmtp pour lui) n'hérite pas de
// celle d'un autre PT : il repart sur notre config.
TEST(NegotiatorH264, PtSansFmtpNHeritePasDeSonVoisin)
{
	std::map<int,int> proposed;
	proposed[96] = VideoCodec::H264;
	proposed[97] = VideoCodec::H264;

	std::map<int,std::string> remote;
	remote[97] = "profile-level-id=64001f;packetization-mode=1";

	Properties props = H264PtProps("42801F", remote);

	NegotiationResult out;
	ASSERT_TRUE(CodecNegotiator::Negotiate(MediaFrame::Video, proposed, props, &props, out));
	ASSERT_EQ(out.codecs.size(), 2u);

	EXPECT_NE(FmtpOf(out,96).find("profile-level-id=42801f"), std::string::npos);
	EXPECT_NE(FmtpOf(out,97).find("profile-level-id=64001f"), std::string::npos);
}
