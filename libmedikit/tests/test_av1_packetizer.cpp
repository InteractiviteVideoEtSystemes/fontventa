/**
 * test_av1_packetizer.cpp — AV1PacketizeTemporalUnit : unité temporelle
 * low-overhead (sortie de l'encodeur) -> paquets RTP conformes au format AV1.
 *
 * Le test central est l'ALLER-RETOUR : ce que le paquetiseur produit, le
 * dépaquetiseur doit le rendre identique à ce qui est entré. C'est la seule
 * vérification qui ne dépende pas de la lecture que ce code fait de la spec —
 * les deux sens ont été écrits séparément, et un aller-retour qui referme
 * n'arrive pas par hasard.
 *
 * Les tests de forme, à côté, verrouillent ce que l'aller-retour ne peut pas
 * voir : les bits d'agrégation réellement posés sur le fil, l'absence du
 * temporal delimiter (que le dépaquetiseur réinsère de toute façon), et le fait
 * que la trame n'est jamais recopiée — un paquet est un préfixe plus une TRANCHE
 * du tampon d'origine, ce qui est ce que MediaFrame::AddRtpPacket sait exprimer.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <av1/av1packetizer.h>
#include <av1/av1depacketizer.h>
#include <vector>
#include <cstring>

namespace
{

// Un OBU au format low-overhead : en-tête avec obu_has_size_field, taille en
// leb128, puis la charge. C'est ce que produit ffmpeg.
void AppendObu(std::vector<BYTE>& out, int type, DWORD payloadLen, BYTE fill)
{
	out.push_back((BYTE)(((type & 0x0f) << 3) | 0x02));	// has_size = 1

	BYTE  leb[8];
	DWORD n = AV1WriteLeb128(leb, payloadLen);
	out.insert(out.end(), leb, leb + n);

	for (DWORD i = 0; i < payloadLen; i++)
		out.push_back(fill);
}

// Une unité temporelle telle que l'encodeur la rend : temporal delimiter, puis
// éventuellement un sequence header, puis la trame.
std::vector<BYTE> TemporalUnit(bool withSeqHdr, DWORD frameLen)
{
	std::vector<BYTE> tu;
	AppendObu(tu, AV1_OBU_TEMPORAL_DELIMITER, 0, 0);
	if (withSeqHdr)
		AppendObu(tu, AV1_OBU_SEQUENCE_HEADER, 10, 0x5a);
	AppendObu(tu, 6 /* OBU_FRAME */, frameLen, 0xa5);
	return tu;
}

// Assemble la charge RTP d'un paquet comme le fait le smoother : le préfixe, puis
// la tranche du tampon de trame.
std::vector<BYTE> Wire(const std::vector<BYTE>& tu, const AV1RtpPacket& p)
{
	std::vector<BYTE> payload(p.prefix, p.prefix + p.prefixLen);
	payload.insert(payload.end(), tu.data() + p.pos, tu.data() + p.pos + p.size);
	return payload;
}

// Parcourt un flux low-overhead et rend (type, taille de charge) par OBU.
std::vector<std::pair<int,DWORD> > Obus(const BYTE* data, DWORD len)
{
	std::vector<std::pair<int,DWORD> > out;
	std::vector<AV1ObuRef> refs;
	if (!AV1ParseObuStream(data, len, refs))
		return out;
	for (size_t i = 0; i < refs.size(); i++)
		out.push_back(std::make_pair(refs[i].type, refs[i].payloadLen));
	return out;
}

const DWORD kMtu = 1350;

} // namespace

// ---------------------------------------------------------------------------
// L'aller-retour : le test qui ne dépend pas de notre lecture de la spec.
// ---------------------------------------------------------------------------

TEST(AV1Packetizer, AllerRetourSurUneImageCle)
{
	std::vector<BYTE> tu = TemporalUnit(/*withSeqHdr*/ true, 200);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	ASSERT_FALSE(packets.empty());

	AV1Depacketizer d;
	for (size_t i = 0; i < packets.size(); i++)
	{
		std::vector<BYTE> w = Wire(tu, packets[i]);
		ASSERT_TRUE(d.AddPayload(w.data(), w.size(), false)) << "paquet " << i;
	}

	DWORD len = 0;
	const BYTE* back = d.GetTemporalUnit(len);
	ASSERT_NE(back, (const BYTE*)NULL);

	// Identique à l'entrée, octet pour octet : même temporal delimiter, même
	// sequence header, même trame.
	ASSERT_EQ(len, tu.size());
	EXPECT_EQ(0, memcmp(back, tu.data(), len));
}

TEST(AV1Packetizer, AllerRetourAvecFragmentation)
{
	// Une trame bien plus grande que la MTU : plusieurs fragments d'un même OBU,
	// donc le chemin à état du dépaquetiseur.
	std::vector<BYTE> tu = TemporalUnit(true, 5000);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	ASSERT_GT(packets.size(), 4u) << "5000 octets pour une MTU de 1350 : au moins 5 paquets";

	AV1Depacketizer d;
	for (size_t i = 0; i < packets.size(); i++)
	{
		std::vector<BYTE> w = Wire(tu, packets[i]);
		ASSERT_LE(w.size(), (size_t)kMtu) << "paquet " << i << " au-dela de la MTU";
		ASSERT_TRUE(d.AddPayload(w.data(), w.size(), false)) << "paquet " << i;
	}

	DWORD len = 0;
	const BYTE* back = d.GetTemporalUnit(len);
	ASSERT_NE(back, (const BYTE*)NULL);
	ASSERT_EQ(len, tu.size());
	EXPECT_EQ(0, memcmp(back, tu.data(), len));
}

TEST(AV1Packetizer, AllerRetourSurUneSuiteDUnitesDontDesInter)
{
	// Le cas réel : une image clé, puis des images inter qui ne portent PAS de
	// sequence header. Le dépaquetiseur doit les rendre décodables grâce à son
	// cache — c'est la propriété qui compte pour un pont média.
	AV1Depacketizer d;

	std::vector<BYTE> key = TemporalUnit(true, 300);
	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(key.data(), key.size(), kMtu, packets));
	for (size_t i = 0; i < packets.size(); i++)
	{
		std::vector<BYTE> w = Wire(key, packets[i]);
		ASSERT_TRUE(d.AddPayload(w.data(), w.size(), false));
	}
	DWORD len = 0;
	ASSERT_NE(d.GetTemporalUnit(len), (const BYTE*)NULL);

	for (int n = 0; n < 3; n++)
	{
		std::vector<BYTE> inter = TemporalUnit(false, 100 + n * 50);
		ASSERT_TRUE(AV1PacketizeTemporalUnit(inter.data(), inter.size(), kMtu, packets));

		for (size_t i = 0; i < packets.size(); i++)
		{
			std::vector<BYTE> w = Wire(inter, packets[i]);
			ASSERT_TRUE(d.AddPayload(w.data(), w.size(), false));
		}

		const BYTE* back = d.GetTemporalUnit(len);
		ASSERT_NE(back, (const BYTE*)NULL) << "unite inter " << n;

		// Le sequence header est réinséré par le dépaquetiseur, donc la sortie
		// n'est pas identique à l'entrée : elle porte un OBU en plus.
		std::vector<std::pair<int,DWORD> > obus = Obus(back, len);
		ASSERT_EQ(obus.size(), 3u);
		EXPECT_EQ(obus[0].first, AV1_OBU_TEMPORAL_DELIMITER);
		EXPECT_EQ(obus[1].first, AV1_OBU_SEQUENCE_HEADER);
		EXPECT_EQ(obus[2].first, 6);
		EXPECT_EQ(obus[2].second, (DWORD)(100 + n * 50));
	}
}

// ---------------------------------------------------------------------------
// La forme sur le fil, que l'aller-retour ne peut pas voir.
// ---------------------------------------------------------------------------

TEST(AV1Packetizer, LeTemporalDelimiterNEstJamaisTransmis)
{
	// La spec RTP l'interdit sur le fil. Aucun paquet ne doit porter ses octets,
	// ni son en-tête dans le préfixe.
	std::vector<BYTE> tu = TemporalUnit(true, 100);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));

	for (size_t i = 0; i < packets.size(); i++)
	{
		// prefix[1] porte l'obu_header quand le paquet ouvre un OBU.
		if (packets[i].prefixLen >= 2)
		{
			const int type = (packets[i].prefix[1] >> 3) & 0x0f;
			EXPECT_NE(type, AV1_OBU_TEMPORAL_DELIMITER) << "paquet " << i;
		}
	}

	// Deux OBU utiles (sequence header + trame), donc deux paquets ici.
	EXPECT_EQ(packets.size(), 2u);
}

TEST(AV1Packetizer, LObuSizeEstTransmisAvecSonDrapeau)
{
	// La longueur de l'élément RTP porte déjà la taille, mais un récepteur qui
	// recolle les charges sans lire les bits d'agrégation n'a que l'obu_size pour
	// délimiter les OBU. Il part donc sur le fil, drapeau compris, et la tranche
	// commence AU leb128, pas après.
	std::vector<BYTE> tu = TemporalUnit(false, 40);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	ASSERT_EQ(packets.size(), 1u);

	const AV1RtpPacket& p = packets[0];
	ASSERT_EQ(p.prefixLen, 2u);
	EXPECT_NE((p.prefix[1] >> 1) & 0x01, 0) << "obu_has_size_field doit rester a 1";
	EXPECT_EQ((p.prefix[1] >> 3) & 0x0f, 6) << "le type doit survivre au transport";

	// La tranche, c'est l'obu_size (1 octet ici) puis 40 octets de 0xa5.
	ASSERT_EQ(p.size, 41u);
	EXPECT_EQ(tu[p.pos], 40) << "le leb128 de la taille ouvre la tranche";
	for (DWORD i = 1; i < p.size; i++)
		ASSERT_EQ(tu[p.pos + i], 0xa5) << "octet " << i;
}

TEST(AV1Packetizer, UnRecepteurNaifRetrouveLesObuDUneImageCle)
{
	// Le cas qui motive la présence de l'obu_size : un récepteur qui se contente
	// de retirer l'octet d'agrégation et de recoller les charges — mediastreamer2
	// (Linphone) est de ceux-là — doit obtenir un flux low-overhead que son
	// décodeur sait délimiter. Sans obu_size, le sequence header d'une image clé
	// avale la trame qui le suit.
	std::vector<BYTE> tu = TemporalUnit(true, 3000);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));

	std::vector<BYTE> naive;
	for (size_t i = 0; i < packets.size(); i++)
	{
		std::vector<BYTE> payload = Wire(tu, packets[i]);
		naive.insert(naive.end(), payload.begin() + 1, payload.end());
	}

	std::vector<std::pair<int,DWORD> > obus = Obus(naive.data(), naive.size());
	ASSERT_EQ(obus.size(), 2u) << "les deux OBU doivent rester delimitables";
	EXPECT_EQ(obus[0].first, AV1_OBU_SEQUENCE_HEADER);
	EXPECT_EQ(obus[0].second, 10u);
	EXPECT_EQ(obus[1].first, 6);
	EXPECT_EQ(obus[1].second, 3000u);
}

TEST(AV1Packetizer, LesBitsDAgregationDUnFragmentSeSuivent)
{
	std::vector<BYTE> tu = TemporalUnit(false, 4000);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));

	// L'arithmétique exacte, écrite pour être vérifiable : la tranche à découper
	// vaut 4002 octets (le leb128 de 4000 en tient 2, et il voyage avec la
	// charge). Le premier paquet perd 2 octets de préfixe (agrégation +
	// obu_header) et porte 1348 octets, les suivants n'en perdent qu'un et
	// portent 1349. 1348 + 1349 + 1305 = 4002.
	ASSERT_EQ(packets.size(), 3u);
	EXPECT_EQ(packets[0].size, 1348u);
	EXPECT_EQ(packets[1].size, 1349u);
	EXPECT_EQ(packets[2].size, 1305u);

	for (size_t i = 0; i < packets.size(); i++)
	{
		const BYTE agg = packets[i].prefix[0];
		const bool Z   = (agg & 0x80) != 0;
		const bool Y   = (agg & 0x40) != 0;
		const int  W   = (agg >> 4) & 0x03;

		EXPECT_EQ(W, 1) << "un element par paquet : la longueur est implicite (paquet "
		                << i << ")";

		// Z : tous sauf le premier continuent un fragment.
		EXPECT_EQ(Z, i != 0) << "paquet " << i;
		// Y : tous sauf le dernier se poursuivent.
		EXPECT_EQ(Y, i + 1 != packets.size()) << "paquet " << i;

		// Un fragment de suite n'a pas d'obu_header à réémettre.
		EXPECT_EQ(packets[i].prefixLen, (i == 0) ? 2u : 1u) << "paquet " << i;
	}

	EXPECT_TRUE(packets.back().mark) << "le bit marqueur ferme l'unite temporelle";
	EXPECT_FALSE(packets.front().mark);
}

TEST(AV1Packetizer, LeBitNMarqueLUniteQuiPorteUnSequenceHeader)
{
	std::vector<AV1RtpPacket> packets;

	std::vector<BYTE> key = TemporalUnit(true, 100);
	ASSERT_TRUE(AV1PacketizeTemporalUnit(key.data(), key.size(), kMtu, packets));
	EXPECT_NE(packets.front().prefix[0] & 0x08, 0)
		<< "une unite portant un sequence header ouvre une sequence codee";
	for (size_t i = 1; i < packets.size(); i++)
		EXPECT_EQ(packets[i].prefix[0] & 0x08, 0)
			<< "N ne vaut que pour le PREMIER paquet (paquet " << i << ")";

	std::vector<BYTE> inter = TemporalUnit(false, 100);
	ASSERT_TRUE(AV1PacketizeTemporalUnit(inter.data(), inter.size(), kMtu, packets));
	EXPECT_EQ(packets.front().prefix[0] & 0x08, 0)
		<< "sans sequence header, l'unite n'ouvre pas de sequence codee";
}

TEST(AV1Packetizer, LaTrameNEstJamaisRecopiee)
{
	// Chaque tranche doit désigner des octets DU tampon d'origine, et l'union des
	// tranches doit couvrir exactement les charges utiles — c'est ce qui permet à
	// PacketizeFrame de ne rien allouer par image.
	std::vector<BYTE> tu = TemporalUnit(true, 3000);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));

	DWORD covered = 0;
	for (size_t i = 0; i < packets.size(); i++)
	{
		ASSERT_LE(packets[i].pos + packets[i].size, (DWORD)tu.size())
			<< "tranche hors du tampon (paquet " << i << ")";
		covered += packets[i].size;
	}

	// 10 octets de sequence header + 3000 de trame, plus leur obu_size (1 octet
	// pour 10, 2 pour 3000) ; seuls les obu_header voyagent dans les préfixes.
	EXPECT_EQ(covered, 3013u);
}

// ---------------------------------------------------------------------------
// Entrées dégénérées : l'encodeur est en amont, mais un OBU mal formé ne doit
// pas produire un flux que personne ne peut lire.
// ---------------------------------------------------------------------------

TEST(AV1Packetizer, UnFluxSansObuSizeEstRefuse)
{
	// Sans obu_size, la fin d'un OBU est indéterminée : on refuse plutôt que de
	// découper au hasard. L'appelant retombe sur sa paquetisation générique.
	std::vector<BYTE> tu;
	tu.push_back((BYTE)(6 << 3));	// has_size = 0
	for (int i = 0; i < 20; i++) tu.push_back(0xa5);

	std::vector<AV1RtpPacket> packets;
	EXPECT_FALSE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	EXPECT_TRUE(packets.empty());
}

TEST(AV1Packetizer, UneTailleDObuHorsTamponEstRefusee)
{
	std::vector<BYTE> tu;
	tu.push_back((BYTE)((6 << 3) | 0x02));
	BYTE  leb[8];
	DWORD n = AV1WriteLeb128(leb, 500);	// annonce 500 octets...
	tu.insert(tu.end(), leb, leb + n);
	for (int i = 0; i < 20; i++) tu.push_back(0xa5);	// ...il y en a 20

	std::vector<AV1RtpPacket> packets;
	EXPECT_FALSE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
}

TEST(AV1Packetizer, UneUniteReduiteAuTemporalDelimiterNeProduitRien)
{
	std::vector<BYTE> tu;
	AppendObu(tu, AV1_OBU_TEMPORAL_DELIMITER, 0, 0);

	std::vector<AV1RtpPacket> packets;
	EXPECT_FALSE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	EXPECT_TRUE(packets.empty());
}

TEST(AV1Packetizer, UneMtuTropPetitePourLePrefixeEstRefusee)
{
	// Deux octets de préfixe minimum sur un début d'OBU : une MTU de 2 ne peut
	// porter aucun octet utile, et boucler dessus serait une boucle infinie.
	std::vector<BYTE> tu = TemporalUnit(false, 100);

	std::vector<AV1RtpPacket> packets;
	EXPECT_FALSE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), 2, packets));
	EXPECT_TRUE(packets.empty());
}

TEST(AV1Packetizer, UnObuSansChargeVautQuandMemeUnPaquet)
{
	// Un OBU réduit à son en-tête (un frame header seul, par exemple) ne doit pas
	// disparaître du flux : le décodeur l'attend.
	std::vector<BYTE> tu;
	AppendObu(tu, AV1_OBU_TEMPORAL_DELIMITER, 0, 0);
	AppendObu(tu, AV1_OBU_SEQUENCE_HEADER, 10, 0x5a);
	AppendObu(tu, 3 /* OBU_FRAME_HEADER */, 0, 0);
	AppendObu(tu, 4 /* OBU_TILE_GROUP */, 50, 0xa5);

	std::vector<AV1RtpPacket> packets;
	ASSERT_TRUE(AV1PacketizeTemporalUnit(tu.data(), tu.size(), kMtu, packets));
	ASSERT_EQ(packets.size(), 3u) << "sequence header, frame header, tile group";

	// Sa tranche se réduit à l'obu_size, qui vaut 0 : un octet, pas zéro.
	ASSERT_EQ(packets[1].size, 1u);
	EXPECT_EQ(tu[packets[1].pos], 0);
	EXPECT_EQ((packets[1].prefix[1] >> 3) & 0x0f, 3);

	// Et l'aller-retour le rend.
	AV1Depacketizer d;
	for (size_t i = 0; i < packets.size(); i++)
	{
		std::vector<BYTE> w = Wire(tu, packets[i]);
		ASSERT_TRUE(d.AddPayload(w.data(), w.size(), false));
	}
	DWORD len = 0;
	const BYTE* back = d.GetTemporalUnit(len);
	ASSERT_NE(back, (const BYTE*)NULL);
	ASSERT_EQ(len, tu.size());
	EXPECT_EQ(0, memcmp(back, tu.data(), len));
}

// ---------------------------------------------------------------------------
// Parcours OBU partagé : la brique dont les deux sens dépendent.
// ---------------------------------------------------------------------------

TEST(AV1ObuStream, LesOffsetsDesignentLObuSansRecopie)
{
	std::vector<BYTE> tu = TemporalUnit(true, 130);	// 130 : leb128 sur 2 octets

	std::vector<AV1ObuRef> refs;
	ASSERT_TRUE(AV1ParseObuStream(tu.data(), tu.size(), refs));
	ASSERT_EQ(refs.size(), 3u);

	EXPECT_EQ(refs[0].type, AV1_OBU_TEMPORAL_DELIMITER);
	EXPECT_EQ(refs[0].payloadLen, 0u);
	EXPECT_EQ(refs[1].type, AV1_OBU_SEQUENCE_HEADER);
	EXPECT_EQ(refs[1].payloadLen, 10u);
	EXPECT_EQ(refs[2].type, 6);
	EXPECT_EQ(refs[2].payloadLen, 130u);

	// Le leb128 de 130 tient sur 2 octets : payloadPos doit en tenir compte.
	EXPECT_EQ(refs[2].payloadPos, refs[2].headerPos + refs[2].headerLen + 2);
	EXPECT_EQ(refs[2].payloadPos + refs[2].payloadLen, (DWORD)tu.size());
}

TEST(AV1ObuStream, UnFluxVideEstUnFluxValideSansObu)
{
	std::vector<AV1ObuRef> refs;
	EXPECT_TRUE(AV1ParseObuStream((const BYTE*)"", 0, refs));
	EXPECT_TRUE(refs.empty());
}
