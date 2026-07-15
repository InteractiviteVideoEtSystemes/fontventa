/**
 * negotest.cpp — harnais de test hors-ligne du négociateur de codecs
 * (CodecNegotiator::Negotiate, cf. medkit/negotiator.h + negotiator.cpp).
 *
 * Vérifie le contrat du retour enrichi d'EndpointStartReceiving (§5.2 de
 * nego_fmtp.md / §6.7 de xmlrpc_jsr309_api.md) : la struct fmtpByPt renvoyée au
 * contrôleur SIP. Ce fichier reconstruit la map EXACTEMENT comme
 * Endpoint::Port::NegotiateReceiving (toujours insérer le PT accepté, même
 * fmtp vide), pour tester la sémantique bout-en-bout :
 *   - présence de la clé = PT accepté ; absence = PT filtré (non supporté) ;
 *   - codec sans fmtp -> valeur "" (chaîne vide), pas d'omission ;
 *   - codec avec fmtp -> chaîne complète (params seuls) ;
 *   - telephone-event -> plage de tonalités ; T140RED -> liste de redondance.
 *
 * Ne dépend ni d'Asterisk ni du mediaserver. Usage : make negotest && ./negotest
 * Sortie : une ligne PASS/FAIL par cas ; code retour != 0 si un cas échoue.
 */
#include "medkit/negotiator.h"
#include <cstdio>
#include <string>
#include <map>

static int g_failures = 0;

// Reconstruit la struct fmtpByPt comme Endpoint::Port::NegotiateReceiving :
// TOUT PT retenu par le négociateur est présent, y compris fmtp vide.
static std::map<int,std::string> BuildFmtpByPt(MediaFrame::Type media,
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

static void Check(const char* label, bool ok)
{
	printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
	if (!ok)
		g_failures++;
}

// Le PT est présent avec la valeur attendue.
static void CheckPresent(const std::map<int,std::string>& m, int pt,
                         const std::string& expected, const char* label)
{
	std::map<int,std::string>::const_iterator it = m.find(pt);
	bool ok = (it != m.end()) && (it->second == expected);
	if (!ok)
	{
		if (it == m.end())
			printf("   -> PT %d ABSENT (attendu present=\"%s\")\n", pt, expected.c_str());
		else
			printf("   -> PT %d = \"%s\" (attendu \"%s\")\n", pt, it->second.c_str(), expected.c_str());
	}
	Check(label, ok);
}

// Le PT est absent (filtré car non supporté).
static void CheckAbsent(const std::map<int,std::string>& m, int pt, const char* label)
{
	bool ok = (m.find(pt) == m.end());
	if (!ok)
		printf("   -> PT %d PRESENT (attendu absent)\n", pt);
	Check(label, ok);
}

int main()
{
	Properties props; // config par défaut

	// --- Cas 1 : audio mixte (PCMU sans fmtp + telephone-event + PT non supporté) ---
	// PCMU(0)=présent "" ; telephone-event(101->100)=présent "0-16" ;
	// PT 200 = codec inconnu -> filtré (absent).
	{
		std::map<int,int> proposed;
		proposed[0]   = AudioCodec::PCMU;            // 0
		proposed[101] = AudioCodec::TELEPHONE_EVENT; // 100
		proposed[200] = 200;                         // inconnu -> non supporté
		std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Audio, proposed, props);
		CheckPresent(m, 0,   "",      "audio: PCMU present avec fmtp vide");
		CheckPresent(m, 101, "0-16",  "audio: telephone-event -> plage 0-16");
		CheckAbsent (m, 200,          "audio: codec inconnu filtre (absent)");
	}

	// --- Cas 2 : vidéo mixte (PCMU non pertinent ; H264 avec fmtp + PT filtré) ---
	// H264(96->99)=présent "profile-level-id=42801f;packetization-mode=1" ;
	// ULPFEC(108) n'est pas un codec média -> filtré.
	{
		std::map<int,int> proposed;
		proposed[96]  = VideoCodec::H264;   // 99
		proposed[108] = VideoCodec::ULPFEC; // 108 -> non supporté
		std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Video, proposed, props);
		CheckPresent(m, 96, "profile-level-id=42801f;packetization-mode=1",
		             "video: H264 present avec sa chaine fmtp");
		CheckAbsent (m, 108, "video: ULPFEC filtre (absent)");
	}

	// --- Cas 3 : texte T140 + T140RED (liste de redondance cohérente) ---
	// T140(106)=présent "" ; T140RED(105)=présent "106/106/106" (ref le PT T140 accepté).
	{
		std::map<int,int> proposed;
		proposed[105] = TextCodec::T140RED; // 105
		proposed[106] = TextCodec::T140;    // 106
		std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Text, proposed, props);
		CheckPresent(m, 106, "",            "texte: T140 present avec fmtp vide");
		CheckPresent(m, 105, "106/106/106", "texte: T140RED -> redondance 106/106/106");
	}

	// --- Cas 4 : non-régression — la struct ne perturbe pas l'ordre/les clés ---
	// (le port reste returnVal[0] côté XML-RPC ; ici on vérifie juste que rien
	//  d'accepté n'est perdu quand tout est sans fmtp).
	{
		std::map<int,int> proposed;
		proposed[0] = AudioCodec::PCMU; // 0
		proposed[8] = AudioCodec::PCMA; // 8
		proposed[9] = AudioCodec::G722; // 9
		std::map<int,std::string> m = BuildFmtpByPt(MediaFrame::Audio, proposed, props);
		Check("audio: 3 codecs sans fmtp tous presents", m.size() == 3 &&
		      m.count(0) && m.count(8) && m.count(9) &&
		      m[0].empty() && m[8].empty() && m[9].empty());
	}

	printf("\n%s (%d echec(s))\n", g_failures ? "ECHEC" : "SUCCES", g_failures);
	return g_failures ? 1 : 0;
}
