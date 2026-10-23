/**
 * test_stunmessage.cpp — sérialisation STUN (RFC 5389 §6).
 *
 * Le champ Length de l'en-tête DOIT valoir exactement le nombre d'octets
 * d'attributs qui suivent. NonAuthenticatedFingerPrint annonçait 24 octets de
 * trop (un MESSAGE-INTEGRITY qu'elle n'écrit pas) : les serveurs STUN stricts
 * (stun.l.google.com, stun.linphone.org) ignoraient alors la requête sans
 * répondre, et `--nat auto` refusait de démarrer. Le parseur maison ne vérifie
 * pas cette longueur, donc un aller-retour Serialize/Parse ne suffit pas à le
 * voir : on lit l'en-tête octet par octet.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/tools.h>
#include <medkit/stunmessage.h>
#include <cstring>

namespace
{
	BYTE kTransId[12] = { 1,2,3,4,5,6,7,8,9,10,11,12 };

	//Champ Length de l'en-tête (octets 2-3)
	DWORD HeaderLength(const BYTE* data)
	{
		return get2(data, 2);
	}
}

// Requête Binding nue, celle de --nat auto : 20 octets, Length = 0.
TEST(StunMessage, RequeteNueAnnonceUneLongueurNulle)
{
	STUNMessage request(STUNMessage::Request, STUNMessage::Binding, kTransId);

	BYTE  buffer[MTU];
	DWORD len = request.NonAuthenticatedFingerPrint(buffer, sizeof(buffer));

	ASSERT_EQ(len, 20u);
	EXPECT_EQ(HeaderLength(buffer), 0u);
	EXPECT_EQ(get2(buffer, 0), 0x0001);            // Binding Request
	EXPECT_EQ(get4(buffer, 4), 0x2112A442u);       // magic cookie
	EXPECT_EQ(0, memcmp(buffer + 8, kTransId, 12));
}

// Avec attributs (dont un non aligné, bourré à 32 bits) : Length = len - 20.
TEST(StunMessage, LongueurEgaleAuxAttributsEcrits)
{
	STUNMessage response(STUNMessage::Response, STUNMessage::Binding, kTransId);
	response.AddUsernameAttribute("abc", "de");    // "de:abc" = 6 o -> 8 bourrés
	response.AddAttribute(STUNMessage::Attribute::Priority, (DWORD)0x6E7F00FF);

	BYTE  buffer[MTU];
	DWORD len = response.NonAuthenticatedFingerPrint(buffer, sizeof(buffer));

	ASSERT_GT(len, 20u);
	EXPECT_EQ(len % 4, 0u);
	EXPECT_EQ(HeaderLength(buffer), len - 20);
}

// La version authentifiée était juste : garde-fou contre une « correction »
// qui la casserait en alignant les deux.
TEST(StunMessage, VersionAuthentifieeCouvreIntegriteEtFingerprint)
{
	STUNMessage request(STUNMessage::Request, STUNMessage::Binding, kTransId);
	request.AddUsernameAttribute("abcd", "efgh");

	BYTE  buffer[MTU];
	DWORD len = request.AuthenticatedFingerPrint(buffer, sizeof(buffer), "secret");

	ASSERT_GT(len, 20u + 24 + 8);
	EXPECT_EQ(HeaderLength(buffer), len - 20);
	// FINGERPRINT en dernier
	EXPECT_EQ(get2(buffer, len - 8), (WORD)STUNMessage::Attribute::FingerPrint);
}
