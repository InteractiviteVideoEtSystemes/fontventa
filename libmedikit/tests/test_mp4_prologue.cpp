/**
 * test_mp4_prologue.cpp — prologue vidéo de mp4writer.
 *
 * Quand la vidéo démarre après le début de l'enregistrement (établissement de
 * l'appel, attente du premier IDR), ce délai ne doit PAS devenir la durée du
 * premier échantillon réel : à la relecture la première image resterait figée
 * pendant tout le délai — symptôme observé en production (« comme si la
 * première I-frame n'était pas valable »).
 *
 * Le délai est donc comblé par des trames noires encodées
 * (mp4writer::WriteVideoPrologue), et la première image réelle garde une durée
 * de trame normale. Ce prologue doit en outre couvrir exactement le même délai
 * que le silence initial de la piste audio, sinon les deux pistes sont décalées.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <mp4v2/mp4v2.h>
#include <medkit/mp4writer.h>
#include <medkit/video.h>
#include <medkit/ffmp4reader.h>
#include <medkit/avcdescriptor.h>
#include <medkit/audio.h>
#include <h264/h264encoder.h>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace {

const int W = 176, H = 144, FPS = 25;
// Délai simulé : assez court pour ne pas alourdir la suite, assez long pour
// produire plusieurs trames de prologue (40 ms par trame). Volontairement NON
// multiple de 40 ms : le prologue doit couvrir le délai réel à la ms, pas un
// multiple de sa période.
const unsigned DELAY_MS   = 330;
const unsigned FRAME_TS   = 40 * 90;      // 40 ms à 90 kHz
const unsigned AUDIO_MS   = 20;           // une trame PCMU

// Bases d'horodatage testées. Le cas 0 est celui de la production : côté
// Asterisk les ts vidéo sont relatifs à la première trame, donc la première
// vaut 0 — il n'existe aucune place AVANT elle pour y loger le prologue. Une
// base élevée masquait ce cas (le prologue était calculé à rebours).
const DWORD    TS_BASE_ASTERISK = 0;
const DWORD    TS_BASE_ABSOLU   = 90000;

// Écrit un MP4 après avoir laissé s'écouler DELAY_MS, comme le fait un
// enregistrement dont la vidéo arrive en retard. L'audio est émis APRÈS la
// première trame vidéo, comme en production (les trames audio sont jetées tant
// que la vidéo n'a pas démarré). @return nb de trames vidéo émises.
int WriteWithDelay(const char* path, bool prologue, int nframes, DWORD tsBase,
                   int naudio = 0, const char* plid = NULL)
{
	MP4FileHandle mp4 = MP4Create(path, 0);
	if (mp4 == MP4_INVALID_FILE_HANDLE) return -1;

	int sent = 0;
	{
		// Détruire le writer AVANT MP4Close (il écrit dans son destructeur).
		mp4writer w(NULL, mp4, /*waitVideo*/ false);
		w.EnableVideoPrologue(prologue);
		w.AddTrack(VideoCodec::H264, W, H, 256, "video", false);

		Properties props;
		if (plid) props.SetProperty("h264.profile-level-id", plid);
		H264Encoder enc(props);
		enc.SetFrameRate(FPS, 256, FPS);
		if (enc.SetSize(W, H) < 0) { MP4Close(mp4); return -1; }

		// Le délai se mesure depuis la construction du mp4writer.
		usleep(DELAY_MS * 1000);

		std::vector<BYTE> pcm(160, 0xFF);        // µ-law : 0xFF = silence
		int audiosent = 0;

		for (int i = 0; i < nframes; i++)
		{
			PictPtr pic = Pict::CreateColor(W, H, 16 + (i * 8) % 200, 128, 128);
			if (!pic) break;

			VideoFramePtr vf = enc.EncodeFrame(pic);
			if (vf == NULL) continue;
			vf->SetTimestamp(tsBase + i * FRAME_TS);
			if (w.ProcessFrame(vf.get()) < 0) break;
			sent++;

			// 2 trames audio de 20 ms par trame vidéo de 40 ms.
			while (audiosent < naudio && audiosent * AUDIO_MS < (DWORD)(sent * 40))
			{
				AudioFrame af(AudioCodec::PCMU, 8000);
				af.SetMedia(&pcm[0], pcm.size());
				af.SetTimestamp(audiosent * AUDIO_MS);   // horloge ms
				if (w.ProcessFrame(&af) < 0) break;
				audiosent++;
			}
		}
	}
	MP4Close(mp4);
	return sent;
}

// Durées de tous les échantillons d'une piste, dans l'échelle de temps de la
// piste. @return false si la piste est absente ou le fichier illisible.
bool ReadSampleDurations(const char* path, const char* type,
                         std::vector<MP4Duration>& durations, DWORD& timeScale)
{
	durations.clear();

	MP4FileHandle mp4 = MP4Read(path);
	if (mp4 == MP4_INVALID_FILE_HANDLE) return false;

	MP4TrackId tid = MP4FindTrackId(mp4, 0, type);
	if (tid == MP4_INVALID_TRACK_ID) { MP4Close(mp4); return false; }

	timeScale = MP4GetTrackTimeScale(mp4, tid);
	MP4SampleId count = MP4GetTrackNumberOfSamples(mp4, tid);

	for (MP4SampleId s = 1; s <= count; s++)             // samples 1-indexés
		durations.push_back(MP4GetSampleDuration(mp4, tid, s));

	MP4Close(mp4);
	return true;
}

// Somme des durées, en ms.
unsigned long SumMs(const std::vector<MP4Duration>& d, size_t from, size_t to,
                    DWORD timeScale)
{
	MP4Duration total = 0;
	for (size_t i = from; i < to && i < d.size(); i++) total += d[i];
	return (unsigned long)(total * 1000 / timeScale);
}

// La piste vidéo n'écrit un échantillon qu'à l'arrivée du suivant : sur
// `nframes` trames réelles émises, la dernière reste en attente. Les
// échantillons attribuables au prologue sont donc `count - (nframes - 1)`.
size_t NbEchantillonsPrologue(const std::vector<MP4Duration>& d, int nframes)
{
	return d.size() - (size_t)(nframes - 1);
}

} // namespace

// --- Le délai est comblé par des trames noires ------------------------------
void VerifieProloguePourBase(const char* path, DWORD tsBase)
{
	const int nframes = 10;

	ASSERT_GT(WriteWithDelay(path, /*prologue*/ true, nframes, tsBase), 0);

	std::vector<MP4Duration> vd; DWORD vts = 0;
	ASSERT_TRUE(ReadSampleDurations(path, MP4_VIDEO_TRACK_TYPE, vd, vts));
	ASSERT_EQ(vts, (DWORD)90000);

	// La première image garde une durée de trame, PAS le délai entier.
	ASSERT_FALSE(vd.empty());
	EXPECT_EQ(vd[0], (MP4Duration)FRAME_TS)
		<< "le délai a été étiré sur le premier échantillon";

	// Des échantillons de prologue se sont ajoutés aux trames réelles.
	ASSERT_GT(vd.size(), (size_t)(nframes - 1)) << "aucun échantillon de prologue écrit";

	// Et ils couvrent au moins le délai simulé (le délai réel le dépasse un peu :
	// création du writer, ouverture de l'encodeur).
	size_t nprologue = NbEchantillonsPrologue(vd, nframes);
	unsigned long prologueMs = SumMs(vd, 0, nprologue, vts);
	EXPECT_GE(prologueMs, (unsigned long)DELAY_MS)
		<< "prologue plus court que le délai (ts de base " << tsBase << ")";
	EXPECT_LE(prologueMs, (unsigned long)(DELAY_MS + 200)) << "prologue anormalement long";

	// Les trames réelles gardent leur cadence : la jonction avec le prologue ne
	// doit ni étirer ni comprimer la vidéo.
	for (size_t i = nprologue; i < vd.size(); i++)
		EXPECT_EQ(vd[i], (MP4Duration)FRAME_TS) << "échantillon réel " << i << " étiré";

	unlink(path);
}

// Cas de production : ts vidéo relatifs, la première trame est à 0.
TEST(Mp4Prologue, DelaiCombleParDesTramesNoires)
{
	VerifieProloguePourBase("/tmp/libmedkit_prologue_on.mp4", TS_BASE_ASTERISK);
}

// Même exigence avec une base d'horodatage élevée.
TEST(Mp4Prologue, DelaiCombleAvecTsAbsolus)
{
	VerifieProloguePourBase("/tmp/libmedkit_prologue_abs.mp4", TS_BASE_ABSOLU);
}

// --- Audio et vidéo comblés du MÊME délai -----------------------------------
// Le prologue vidéo et le silence initial de l'audio doivent s'achever au même
// instant. Deux mesures d'horloge distinctes suffisaient à les décaler : le
// temps d'encodage du prologue s'intercalait entre elles (37 ms en production).
TEST(Mp4Prologue, AudioEtVideoDecalesDuMemeDelai)
{
	const char* path = "/tmp/libmedkit_prologue_sync.mp4";
	const int   nframes = 10;
	const int   naudio  = 20;              // 20 x 20 ms = 400 ms, couvre la vidéo

	ASSERT_GT(WriteWithDelay(path, /*prologue*/ true, nframes, TS_BASE_ASTERISK, naudio), 0);

	std::vector<MP4Duration> vd, ad; DWORD vts = 0, ats = 0;
	ASSERT_TRUE(ReadSampleDurations(path, MP4_VIDEO_TRACK_TYPE, vd, vts));
	ASSERT_TRUE(ReadSampleDurations(path, MP4_AUDIO_TRACK_TYPE, ad, ats));

	size_t nprologue = NbEchantillonsPrologue(vd, nframes);
	unsigned long prologueMs = SumMs(vd, 0, nprologue, vts);

	// L'audio est écrit sans attente : ses échantillons de silence sont ceux qui
	// précèdent les trames réellement émises.
	ASSERT_GT(ad.size(), (size_t)0);
	unsigned long audioTotalMs = SumMs(ad, 0, ad.size(), ats);
	unsigned long audioReelMs  = SumMs(ad, ad.size() - (size_t)naudio, ad.size(), ats);
	unsigned long silenceMs    = audioTotalMs - audioReelMs;

	EXPECT_GT(silenceMs, 0UL) << "aucun silence initial sur la piste audio";
	EXPECT_EQ(silenceMs, prologueMs)
		<< "audio et vidéo comblés de délais différents : " << silenceMs
		<< " ms de silence contre " << prologueMs << " ms de prologue";

	unlink(path);
}

// --- profile-level-id cohérent de bout en bout ------------------------------
// Deux exigences distinctes, toutes deux constatées en défaut en production
// (pcap mp4play : SPS 42801F émis alors que 420016 était négocié) :
//  1. le SPS du prologue doit reprendre celui de la vidéo réelle — c'est lui qui
//     est émis en tête de flux à la relecture, et un décodeur initialisé sur un
//     niveau supérieur au niveau négocié peut refuser la suite ;
//  2. l'en-tête de l'avcC doit refléter ce SPS. Create() écrit les valeurs par
//     défaut de la classe (niveau 1.3) avant d'avoir vu un SPS.
TEST(Mp4Prologue, ProfileLevelIdCoherentDansLAvcC)
{
	const char* path = "/tmp/libmedkit_prologue_plid.mp4";

	// Vidéo réelle en 42C016 (profil 66, niveau 2.2) et non au défaut 42801F.
	ASSERT_GT(WriteWithDelay(path, /*prologue*/ true, 10, TS_BASE_ASTERISK, 0, "42C016"), 0);

	MP4FileHandle mp4 = MP4Read(path);
	ASSERT_NE(mp4, MP4_INVALID_FILE_HANDLE);
	MP4TrackId tid = MP4FindTrackId(mp4, 0, MP4_VIDEO_TRACK_TYPE);
	ASSERT_NE(tid, MP4_INVALID_TRACK_ID);

	// 1. Le SPS réellement stocké (celui qui sera émis en RTP).
	uint8_t** sps = NULL; uint32_t* spsLen = NULL;
	uint8_t** pps = NULL; uint32_t* ppsLen = NULL;
	ASSERT_TRUE(MP4GetTrackH264SeqPictHeaders(mp4, tid, &sps, &spsLen, &pps, &ppsLen));
	ASSERT_TRUE(sps != NULL && sps[0] != NULL && spsLen[0] >= 4) << "aucun SPS stocké";
	EXPECT_EQ((int)sps[0][1], 0x42) << "profile_idc du SPS";
	EXPECT_EQ((int)sps[0][2], 0xC0) << "contraintes du SPS";
	EXPECT_EQ((int)sps[0][3], 0x16)
		<< "le SPS du prologue n'a pas le niveau de la vidéo réelle";

	// 2. L'en-tête de l'avcC, recalé sur ce SPS.
	uint64_t prof = 0, compat = 0, level = 0;
	EXPECT_TRUE(MP4GetTrackIntegerProperty(mp4, tid,
		"mdia.minf.stbl.stsd.avc1.avcC.AVCProfileIndication", &prof));
	EXPECT_TRUE(MP4GetTrackIntegerProperty(mp4, tid,
		"mdia.minf.stbl.stsd.avc1.avcC.profile_compatibility", &compat));
	EXPECT_TRUE(MP4GetTrackIntegerProperty(mp4, tid,
		"mdia.minf.stbl.stsd.avc1.avcC.AVCLevelIndication", &level));
	EXPECT_EQ((int)prof,   0x42);
	EXPECT_EQ((int)compat, 0xC0);
	EXPECT_EQ((int)level,  0x16)
		<< "l'en-tête de l'avcC n'a pas été recalé sur le SPS";

	MP4Close(mp4);
	unlink(path);
}

// --- Un échantillon sync doit porter un IDR -----------------------------------
// Le dépacketiseur marque « intra » toute trame porteuse de SPS/PPS (points de
// rafraîchissement x264 intra_refresh) ; marquer sync une trame P sans IDR
// poussait le lecteur à lui préfixer les paramètres de l'avcC — ceux du
// PROLOGUE — et le décodeur du pair se re-calait dessus en plein flux réel
// (log2_max_frame_num 4 contre 6 : slices illisibles, constaté en production).
TEST(Mp4Prologue, EchantillonSyncPorteUnIdr)
{
	const char* path = "/tmp/libmedkit_prologue_sync_idr.mp4";

	ASSERT_GT(WriteWithDelay(path, /*prologue*/ true, 10, TS_BASE_ASTERISK), 0);

	MP4FileHandle mp4 = MP4Read(path);
	ASSERT_NE(mp4, MP4_INVALID_FILE_HANDLE);
	MP4TrackId tid = MP4FindTrackId(mp4, 0, MP4_VIDEO_TRACK_TYPE);
	ASSERT_NE(tid, MP4_INVALID_TRACK_ID);

	MP4SampleId count = MP4GetTrackNumberOfSamples(mp4, tid);
	int nsync = 0;
	for (MP4SampleId sid = 1; sid <= count; sid++)
	{
		bool isSync = false;
		ASSERT_TRUE(MP4GetSampleSync(mp4, tid, sid) != -1);
		if (MP4GetSampleSync(mp4, tid, sid) != 1) continue;
		nsync++;

		uint8_t* buf = NULL; uint32_t len = 0;
		ASSERT_TRUE(MP4ReadSample(mp4, tid, sid, &buf, &len));
		bool idr = false;
		uint32_t off = 0;
		while (off + 4 < len)
		{
			uint32_t n = ((uint32_t)buf[off]<<24)|((uint32_t)buf[off+1]<<16)|((uint32_t)buf[off+2]<<8)|buf[off+3];
			if (n == 0 || off + 4 + n > len) break;
			if ((buf[off+4] & 0x1F) == 0x05) { idr = true; break; }
			off += 4 + n;
		}
		free(buf);
		EXPECT_TRUE(idr) << "échantillon sync #" << sid << " sans IDR";
	}
	EXPECT_GT(nsync, 0) << "aucun échantillon sync";

	MP4Close(mp4);
	unlink(path);
}

// --- Attente de l'IDR sans prologue créé ------------------------------------
// Si la vidéo arrive tout de suite (délai < une période de trame), aucun
// PictureStreamer n'est créé. Le chemin d'attente de l'IDR, qui remplace les
// P-frames par des trames noires, doit alors s'abstenir au lieu de déréférencer
// un pointeur nul.
TEST(Mp4Prologue, AttenteIdrSansPictureStreamer)
{
	const char* path = "/tmp/libmedkit_prologue_noidr.mp4";

	MP4FileHandle mp4 = MP4Create(path, 0);
	ASSERT_NE(mp4, MP4_INVALID_FILE_HANDLE);

	{
		// waitVideo actif : le writer attend une intra avant d'enregistrer.
		mp4writer w(NULL, mp4, /*waitVideo*/ true);
		w.EnableVideoPrologue(true);
		w.AddTrack(VideoCodec::H264, W, H, 256, "video", false);

		Properties props;
		H264Encoder enc(props);
		enc.SetFrameRate(FPS, 256, FPS);
		ASSERT_GE(enc.SetSize(W, H), 0);

		// Deux images IDENTIQUES : la 2e ne déclenche pas de coupure de scène,
		// c'est donc une P-frame. Pas de usleep : le délai reste sous 40 ms.
		PictPtr pic = Pict::CreateColor(W, H, 64, 128, 128);
		ASSERT_TRUE(pic != nullptr);

		VideoFramePtr vf = enc.EncodeFrame(pic);   // intra, non transmise
		ASSERT_TRUE(vf != NULL);

		vf = enc.EncodeFrame(pic);
		ASSERT_TRUE(vf != NULL);
		if (vf->IsIntra()) GTEST_SKIP() << "l'encodeur a produit une 2e intra";

		// Première trame vue par le writer : une P-frame, donc attente de l'IDR
		// alors qu'aucun prologue n'a pu être créé.
		vf->SetTimestamp(0);
		EXPECT_NO_FATAL_FAILURE(w.ProcessFrame(vf.get()));
		EXPECT_EQ(w.IsVideoStarted(), 0) << "la vidéo ne doit pas démarrer sur une P-frame";
	}
	MP4Close(mp4);
	unlink(path);
}

// --- Prologue désactivé : pas d'étirement non plus ---------------------------
// Sans prologue, le délai d'établissement est simplement ignoré (la vidéo
// démarre à t=0) ; il ne doit surtout pas atterrir sur la 1re image.
TEST(Mp4Prologue, SansProloguePasDEtirement)
{
	const char* path = "/tmp/libmedkit_prologue_off.mp4";
	const int   nframes = 10;

	ASSERT_GT(WriteWithDelay(path, /*prologue*/ false, nframes, TS_BASE_ASTERISK), 0);

	std::vector<MP4Duration> vd; DWORD vts = 0;
	ASSERT_TRUE(ReadSampleDurations(path, MP4_VIDEO_TRACK_TYPE, vd, vts));
	ASSERT_FALSE(vd.empty());

	EXPECT_LT(vd[0], (MP4Duration)(DELAY_MS * 90 / 2))
		<< "le délai a été étiré sur le premier échantillon";
	// Aucune trame noire : seuls les échantillons réels sont présents.
	EXPECT_LE(vd.size(), (size_t)nframes);
	unlink(path);
}
