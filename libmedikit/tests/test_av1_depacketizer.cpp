/**
 * test_av1_depacketizer.cpp — AV1Depacketizer : charges RTP AV1 -> unité
 * temporelle au format low-overhead, le seul que libdav1d sait lire.
 *
 * Le cas adverse central reproduit la panne du 2026-08-12 : sans dépaquetiseur,
 * l'accumulation brute donnait au décodeur l'octet d'agrégation RTP en tête de
 * charge, qu'il lisait comme un obu_header — d'où les « Unknown OBU type 11 »
 * (un type qui n'existe pas dans la spec) et « No sequence header available » en
 * boucle sur un appel AV1 ↔ AV1 par ailleurs parfaitement négocié. Les tests
 * vérifient donc, à chaque fois, que la sortie est un flux d'OBU DÉLIMITABLE :
 * c'est cette propriété-là qui manquait, pas la présence d'octets.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <av1/av1depacketizer.h>
#include <vector>
#include <cstring>

namespace
{

// Un OBU tel qu'il apparaît dans le flux rendu au décodeur.
struct Obu
{
	int   type;
	DWORD payloadLen;
};

// Parcourt un flux low-overhead (chaque OBU porte son obu_size en leb128) et
// rend la liste des OBU. false si la chaîne est incohérente — c'est exactement
// la lecture que fait libdav1d, et ce qu'aucune sortie ne réussissait avant.
bool ParseObuStream(const BYTE* data, DWORD len, std::vector<Obu>& out)
{
	out.clear();
	DWORD pos = 0;

	while (pos < len)
	{
		const BYTE hdr = data[pos];

		// obu_forbidden_bit doit être nul, obu_has_size_field doit être posé :
		// sans lui rien ne délimite l'OBU suivant.
		if (hdr & 0x80)         return false;
		if (!((hdr >> 1) & 1))  return false;

		const int  type    = (hdr >> 3) & 0x0f;
		const bool extFlag = (hdr >> 2) & 1;

		DWORD p = pos + 1 + (extFlag ? 1 : 0);
		if (p > len) return false;

		size_t consumed = 0;
		QWORD  size     = 0;
		if (!AV1ReadLeb128(data + p, len - p, consumed, size)) return false;
		p += consumed;

		if (p + size > len) return false;

		Obu o;
		o.type       = type;
		o.payloadLen = (DWORD)size;
		out.push_back(o);

		pos = p + (DWORD)size;
	}

	return pos == len;
}

// Construit un OBU « tel qu'il voyage en RTP » : en-tête sans obu_size (ce que
// la spec recommande), suivi de sa charge.
std::vector<BYTE> WireObu(int type, DWORD payloadLen, BYTE fill = 0xa5)
{
	std::vector<BYTE> o;
	o.push_back((BYTE)((type & 0x0f) << 3));	// has_size = 0, pas d'extension
	for (DWORD i = 0; i < payloadLen; i++)
		o.push_back(fill);
	return o;
}

// Assemble une charge RTP AV1 : octet d'agrégation puis les éléments.
// `elements` : les OBU (ou morceaux d'OBU) à placer. W=0 -> tous les éléments
// portent leur longueur ; W=n -> le dernier ne la porte pas.
std::vector<BYTE> Payload(bool Z, bool Y, int W, bool N,
                          const std::vector<std::vector<BYTE> >& elements)
{
	std::vector<BYTE> p;

	BYTE agg = 0;
	if (Z) agg |= 0x80;
	if (Y) agg |= 0x40;
	agg |= (BYTE)((W & 0x03) << 4);
	if (N) agg |= 0x08;
	p.push_back(agg);

	for (size_t i = 0; i < elements.size(); i++)
	{
		const bool implicitLen = (W != 0 && i == elements.size() - 1);

		if (!implicitLen)
		{
			BYTE  leb[8];
			DWORD n = AV1WriteLeb128(leb, elements[i].size());
			p.insert(p.end(), leb, leb + n);
		}

		p.insert(p.end(), elements[i].begin(), elements[i].end());
	}

	return p;
}

// Une charge complète et autonome : sequence header + frame, un seul paquet.
std::vector<BYTE> KeyPayload(DWORD frameLen = 40)
{
	std::vector<std::vector<BYTE> > elems;
	elems.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 10));
	elems.push_back(WireObu(6 /* OBU_FRAME */, frameLen));
	return Payload(false, false, 2, true, elems);
}

} // namespace

// ---------------------------------------------------------------------------
// Le cas nominal, et la propriété qui manquait : la sortie est délimitable.
// ---------------------------------------------------------------------------

TEST(AV1Depacketizer, UnPaquetAutonomeDonneUneUniteLisible)
{
	AV1Depacketizer d;
	std::vector<BYTE> p = KeyPayload();

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);
	ASSERT_GT(len, 0u);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus))
		<< "le flux rendu n'est pas delimitable : c'est LA panne du 2026-08-12";

	// Temporal delimiter rétabli en tête, puis sequence header, puis la trame.
	ASSERT_EQ(obus.size(), 3u);
	EXPECT_EQ(obus[0].type, AV1_OBU_TEMPORAL_DELIMITER);
	EXPECT_EQ(obus[0].payloadLen, 0u);
	EXPECT_EQ(obus[1].type, AV1_OBU_SEQUENCE_HEADER);
	EXPECT_EQ(obus[2].type, 6);
	EXPECT_EQ(obus[2].payloadLen, 40u);
}

TEST(AV1Depacketizer, LOctetDAgregationNeFinitJamaisDansLeFlux)
{
	// Le défaut historique en une assertion : l'octet d'agrégation valait ici
	// 0x28 (W=2, N=1) et se retrouvait en tête du flux donné au décodeur, qui le
	// lisait comme un obu_header — type (0x28>>3)&0x0f = 5, has_size = 0.
	AV1Depacketizer d;
	std::vector<BYTE> p = KeyPayload();
	const BYTE agg = p[0];

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	// Le premier octet est le temporal delimiter, pas l'agrégation.
	EXPECT_EQ(tu[0], 0x12);
	EXPECT_NE(tu[0], agg);
}

TEST(AV1Depacketizer, TousLesElementsPortentLeurLongueurQuandWEstNul)
{
	AV1Depacketizer d;

	std::vector<std::vector<BYTE> > elems;
	elems.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 12));
	elems.push_back(WireObu(3 /* OBU_FRAME_HEADER */, 5));
	elems.push_back(WireObu(4 /* OBU_TILE_GROUP */, 60));
	std::vector<BYTE> p = Payload(false, false, 0, true, elems);

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus));
	ASSERT_EQ(obus.size(), 4u);
	EXPECT_EQ(obus[3].payloadLen, 60u);
}

// ---------------------------------------------------------------------------
// Fragmentation : la différence de fond avec VP8, et la seule partie à état.
// ---------------------------------------------------------------------------

TEST(AV1Depacketizer, UnObuFragmenteSurTroisPaquetsEstRecolle)
{
	AV1Depacketizer d;

	// Paquet 1 : sequence header complet + début d'une trame (Y=1).
	std::vector<BYTE> head = WireObu(6, 0);	// en-tête d'OBU seul
	std::vector<BYTE> part1 = head;
	for (int i = 0; i < 30; i++) part1.push_back(0x11);

	std::vector<std::vector<BYTE> > e1;
	e1.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 10));
	e1.push_back(part1);
	std::vector<BYTE> p1 = Payload(false, true, 2, true, e1);

	// Paquet 2 : suite (Z=1, Y=1).
	std::vector<BYTE> part2(40, 0x22);
	std::vector<std::vector<BYTE> > e2;
	e2.push_back(part2);
	std::vector<BYTE> p2 = Payload(true, true, 1, false, e2);

	// Paquet 3 : fin (Z=1, Y=0).
	std::vector<BYTE> part3(25, 0x33);
	std::vector<std::vector<BYTE> > e3;
	e3.push_back(part3);
	std::vector<BYTE> p3 = Payload(true, false, 1, false, e3);

	ASSERT_TRUE(d.AddPayload(p1.data(), p1.size(), false));
	ASSERT_TRUE(d.AddPayload(p2.data(), p2.size(), false));
	ASSERT_TRUE(d.AddPayload(p3.data(), p3.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus));
	ASSERT_EQ(obus.size(), 3u);
	// La trame recollée : 30 + 40 + 25 octets de charge.
	EXPECT_EQ(obus[2].type, 6);
	EXPECT_EQ(obus[2].payloadLen, 95u);
}

TEST(AV1Depacketizer, UnFragmentJamaisReferméNeDonneAucuneUnite)
{
	// Le pendant AV1 du bit E jamais vu en FU-A : le bit marqueur arrive alors
	// qu'un OBU est encore ouvert. Rendre l'unité tronquée ferait décoder du
	// partiel ; on n'en rend aucune, et l'appelant demande une image clé.
	AV1Depacketizer d;

	std::vector<BYTE> part1 = WireObu(6, 0);
	for (int i = 0; i < 30; i++) part1.push_back(0x11);

	std::vector<std::vector<BYTE> > e1;
	e1.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 10));
	e1.push_back(part1);
	std::vector<BYTE> p1 = Payload(false, true, 2, true, e1);

	ASSERT_TRUE(d.AddPayload(p1.data(), p1.size(), false));

	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
	EXPECT_EQ(len, 0u);
}

TEST(AV1Depacketizer, UneSuiteOrphelineEstJeteeSansEmporterLesElementsSuivants)
{
	// Prise de flux en cours : le premier paquet vu annonce Z=1 alors qu'on n'a
	// jamais eu la tête du fragment. Cet élément est perdu, pas les autres.
	AV1Depacketizer d;

	std::vector<BYTE> orphan(20, 0x99);
	std::vector<std::vector<BYTE> > e;
	e.push_back(orphan);
	e.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 10));
	e.push_back(WireObu(6, 30));
	std::vector<BYTE> p = Payload(true, false, 3, false, e);

	// L'unité est signalée endommagée...
	EXPECT_FALSE(d.AddPayload(p.data(), p.size(), false));
	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);

	// ...mais le sequence header a été capturé au passage : l'unité SUIVANTE,
	// même sans sequence header à elle, redevient décodable. C'est tout
	// l'intérêt du cache.
	EXPECT_TRUE(d.HasSequenceHeader());

	std::vector<std::vector<BYTE> > e2;
	e2.push_back(WireObu(6, 30));
	std::vector<BYTE> p2 = Payload(false, false, 1, false, e2);

	ASSERT_TRUE(d.AddPayload(p2.data(), p2.size(), false));
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus));
	ASSERT_EQ(obus.size(), 3u);
	EXPECT_EQ(obus[1].type, AV1_OBU_SEQUENCE_HEADER) << "sequence header non reemis";
}

// ---------------------------------------------------------------------------
// Sequence header : ce que la spec RTP ne répète pas, et qu'un pont doit tenir.
// ---------------------------------------------------------------------------

TEST(AV1Depacketizer, SansSequenceHeaderAucuneUniteNEstRendue)
{
	// « No sequence header available » de la trace : mieux vaut ne rien donner au
	// décodeur et faire demander une image clé que de lui faire échouer chaque
	// unité en silence.
	AV1Depacketizer d;

	std::vector<std::vector<BYTE> > e;
	e.push_back(WireObu(6, 50));
	std::vector<BYTE> p = Payload(false, false, 1, false, e);

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
	EXPECT_FALSE(d.HasSequenceHeader());
}

TEST(AV1Depacketizer, LeSequenceHeaderEstReemisSurChaqueUniteQuiNEnPortePas)
{
	AV1Depacketizer d;

	std::vector<BYTE> key = KeyPayload();
	ASSERT_TRUE(d.AddPayload(key.data(), key.size(), false));
	DWORD len = 0;
	ASSERT_NE(d.GetTemporalUnit(len), (const BYTE*)NULL);

	// Trois unités « inter » de suite, aucune ne portant de sequence header.
	for (int i = 0; i < 3; i++)
	{
		std::vector<std::vector<BYTE> > e;
		e.push_back(WireObu(6, 20));
		std::vector<BYTE> p = Payload(false, false, 1, false, e);

		ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));
		const BYTE* tu = d.GetTemporalUnit(len);
		ASSERT_NE(tu, (const BYTE*)NULL) << "unite inter " << i << " perdue";

		std::vector<Obu> obus;
		ASSERT_TRUE(ParseObuStream(tu, len, obus));
		ASSERT_EQ(obus.size(), 3u);
		EXPECT_EQ(obus[1].type, AV1_OBU_SEQUENCE_HEADER);
	}
}

TEST(AV1Depacketizer, UnePerteAbandonneLUniteMaisPasLaSequence)
{
	AV1Depacketizer d;

	std::vector<BYTE> key = KeyPayload();
	ASSERT_TRUE(d.AddPayload(key.data(), key.size(), false));
	DWORD len = 0;
	ASSERT_NE(d.GetTemporalUnit(len), (const BYTE*)NULL);

	// Paquet suivant signalé « perte avant moi ».
	std::vector<std::vector<BYTE> > e;
	e.push_back(WireObu(6, 20));
	std::vector<BYTE> p = Payload(false, false, 1, false, e);

	EXPECT_FALSE(d.AddPayload(p.data(), p.size(), /*lost*/ true));
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);

	// Le sequence header survit à la perte : l'unité d'après repart seule.
	EXPECT_TRUE(d.HasSequenceHeader());
	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));
	EXPECT_NE(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

TEST(AV1Depacketizer, LAppelDeVidangeNeRendRien)
{
	// VideoDecoderJoinableWorker appelle DecodePacket(NULL,0,1,1) pour vider ce
	// qui reste quand il détecte une perte : la charge est nulle.
	AV1Depacketizer d;

	std::vector<BYTE> key = KeyPayload();
	ASSERT_TRUE(d.AddPayload(key.data(), key.size(), false));

	EXPECT_FALSE(d.AddPayload(NULL, 0, true));
	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

// ---------------------------------------------------------------------------
// Charges malformées : un pair hostile ou buggé ne doit pas nous faire sortir
// des bornes. Le décodeur est en aval, mais le dépaquetiseur est le premier à
// lire des octets venus du réseau.
// ---------------------------------------------------------------------------

TEST(AV1Depacketizer, UneLongueurDElementQuiDepasseLaChargeEstRefusee)
{
	AV1Depacketizer d;

	// W=0, un élément annonçant 200 octets dans une charge qui n'en a que 10.
	std::vector<BYTE> p;
	p.push_back(0x08);	// N=1, W=0
	BYTE leb[8];
	DWORD n = AV1WriteLeb128(leb, 200);
	p.insert(p.end(), leb, leb + n);
	for (int i = 0; i < 10; i++) p.push_back(0x77);

	EXPECT_FALSE(d.AddPayload(p.data(), p.size(), false));
	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

TEST(AV1Depacketizer, UnCompteDElementsSuperieurAuContenuEstRefuse)
{
	AV1Depacketizer d;

	// W=3 annoncé, un seul élément présent.
	std::vector<std::vector<BYTE> > e;
	e.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 4));
	std::vector<BYTE> p = Payload(false, false, 3, true, e);

	EXPECT_FALSE(d.AddPayload(p.data(), p.size(), false));
	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

TEST(AV1Depacketizer, UneChargeReduiteALOctetDAgregationNeCasseRien)
{
	AV1Depacketizer d;
	BYTE agg = 0x08;	// N=1, W=0, aucun élément

	EXPECT_TRUE(d.AddPayload(&agg, 1, false));
	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

TEST(AV1Depacketizer, LAccumulationEstBorneeQuandLeBitMarqueurNArriveJamais)
{
	// Un émetteur cassé (ou hostile) qui ne pose jamais le bit marqueur ferait
	// grossir le tampon sans fin : l'entrée vient du réseau, la borne est de la
	// sûreté, pas du réglage.
	AV1Depacketizer d;

	std::vector<BYTE> key = KeyPayload();
	ASSERT_TRUE(d.AddPayload(key.data(), key.size(), false));

	// 64 Ko par paquet, sans jamais demander l'unité : la borne doit tomber bien
	// avant que la mémoire ne parle.
	bool refused = false;
	for (int i = 0; i < 200 && !refused; i++)
	{
		std::vector<std::vector<BYTE> > e;
		e.push_back(WireObu(4 /* OBU_TILE_GROUP */, 64 * 1024));
		std::vector<BYTE> p = Payload(false, false, 1, false, e);

		refused = !d.AddPayload(p.data(), p.size(), false);
	}

	EXPECT_TRUE(refused) << "aucune borne : l'accumulation suit le reseau sans limite";

	DWORD len = 0;
	EXPECT_EQ(d.GetTemporalUnit(len), (const BYTE*)NULL);

	// Et l'unité SUIVANTE repart proprement : la borne n'est pas un état terminal.
	std::vector<std::vector<BYTE> > e;
	e.push_back(WireObu(6, 30));
	std::vector<BYTE> p = Payload(false, false, 1, false, e);
	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));
	EXPECT_NE(d.GetTemporalUnit(len), (const BYTE*)NULL);
}

TEST(AV1Depacketizer, LeTemporalDelimiterRecuEstFiltreEtNonDuplique)
{
	// La spec l'interdit sur le fil ; un pair qui l'envoie quand même ne doit pas
	// produire DEUX temporal delimiters, ce qui ferait deux unités pour le
	// décodeur là où il n'y en a qu'une.
	AV1Depacketizer d;

	std::vector<std::vector<BYTE> > e;
	e.push_back(WireObu(AV1_OBU_TEMPORAL_DELIMITER, 0));
	e.push_back(WireObu(AV1_OBU_SEQUENCE_HEADER, 10));
	e.push_back(WireObu(6, 30));
	std::vector<BYTE> p = Payload(false, false, 3, true, e);

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus));
	ASSERT_EQ(obus.size(), 3u);
	EXPECT_EQ(obus[0].type, AV1_OBU_TEMPORAL_DELIMITER);
	EXPECT_NE(obus[1].type, AV1_OBU_TEMPORAL_DELIMITER);
	EXPECT_NE(obus[2].type, AV1_OBU_TEMPORAL_DELIMITER);
}

TEST(AV1Depacketizer, UnObuQuiPorteDejaSonObuSizeEstAccepte)
{
	// obu_has_size_field=1 est permis (juste redondant) : la longueur d'élément
	// fait foi, et la sortie ne doit pas se retrouver avec deux tailles.
	AV1Depacketizer d;

	std::vector<BYTE> obu;
	obu.push_back((BYTE)((AV1_OBU_SEQUENCE_HEADER << 3) | 0x02));	// has_size = 1
	BYTE leb[8];
	DWORD n = AV1WriteLeb128(leb, 10);
	obu.insert(obu.end(), leb, leb + n);
	for (int i = 0; i < 10; i++) obu.push_back(0x5a);

	std::vector<std::vector<BYTE> > e;
	e.push_back(obu);
	e.push_back(WireObu(6, 30));
	std::vector<BYTE> p = Payload(false, false, 2, true, e);

	ASSERT_TRUE(d.AddPayload(p.data(), p.size(), false));

	DWORD len = 0;
	const BYTE* tu = d.GetTemporalUnit(len);
	ASSERT_NE(tu, (const BYTE*)NULL);

	std::vector<Obu> obus;
	ASSERT_TRUE(ParseObuStream(tu, len, obus));
	ASSERT_EQ(obus.size(), 3u);
	EXPECT_EQ(obus[1].type, AV1_OBU_SEQUENCE_HEADER);
	EXPECT_EQ(obus[1].payloadLen, 10u);
}

// ---------------------------------------------------------------------------
// leb128 : le format de longueur, partagé avec le codec. Les tailles ≥ 128 sont
// le seul endroit où une divergence se verrait, donc le seul à tester.
// ---------------------------------------------------------------------------

TEST(AV1Leb128, AllerRetourSurLesTaillesQuiChangentDeLongueur)
{
	const QWORD values[] = { 0, 1, 127, 128, 129, 16383, 16384, 1000000 };

	for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
	{
		BYTE  buf[8];
		DWORD n = AV1WriteLeb128(buf, values[i]);
		ASSERT_GT(n, 0u);

		size_t consumed = 0;
		QWORD  back     = 0;
		ASSERT_TRUE(AV1ReadLeb128(buf, n, consumed, back)) << "valeur " << values[i];
		EXPECT_EQ(back, values[i]);
		EXPECT_EQ(consumed, (size_t)n);
	}
}

TEST(AV1Leb128, UnLeb128TronqueEstRefuse)
{
	// Tous les octets ont le bit de continuation : la valeur n'est jamais close.
	const BYTE truncated[3] = { 0x80, 0x80, 0x80 };

	size_t consumed = 0;
	QWORD  value    = 0;
	EXPECT_FALSE(AV1ReadLeb128(truncated, sizeof(truncated), consumed, value));
}
