/**
 * test_mp4_roundtrip.cpp — round-trip d'écriture/lecture MP4, SANS fixture
 * externe : on écrit un MP4 (piste audio PCMU + piste vidéo H264) via mp4writer,
 * puis on le relit via Mp4FfReader. Les trames H264 sont produites par
 * l'encodeur de la lib (H264Encoder → x264/VAAPI), donc bien formées — ce qui
 * exerce réellement la relecture vidéo (contrairement à la fixture record.mp4
 * dont le flux H264 est défectueux, cf. libmedikit_tests_plan.md).
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <mp4v2/mp4v2.h>
#include <medkit/mp4writer.h>
#include <medkit/ffmp4reader.h>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <h264/h264encoder.h>
#include <cstdio>
#include <vector>
#include <unistd.h>

namespace {

const int   W = 176, H = 144, FPS = 10, NFRAMES = 12;
const char* PATH = "/tmp/libmedkit_roundtrip.mp4";

// Sort de la fonction (bool) en échec si l'expression renvoie < 0.
#define RET_FALSE_IF_NEG(expr) do { if ((expr) < 0) return false; } while(0)

// Écrit un MP4 audio PCMU + vidéo H264. @return true si l'écriture a produit
// au moins une trame vidéo et une trame audio.
bool WriteMp4(int& videoWritten, int& audioWritten)
{
	videoWritten = 0; audioWritten = 0;

	MP4FileHandle mp4 = MP4Create(PATH, 0);
	if (mp4 == MP4_INVALID_FILE_HANDLE)
		return false;

	// IMPORTANT : le mp4writer doit être DÉTRUIT avant MP4Close — son destructeur
	// écrit encore dans le handle (MP4TagsStore). L'inverse déclenche l'assert
	// mp4v2 « AddDescendantAtoms (pAncestorAtom) » (écriture sur handle fermé).
	{
		mp4writer w(NULL, mp4, /*waitVideo*/ false);
		w.EnableVideoPrologue(false); // pas d'image de garde 640x480 (mélange de tailles)
		w.AddTrack(VideoCodec::H264, W, H, 256, "video", false);
		w.AddTrack(AudioCodec::PCMU, 8000, "audio");

		// Encodeur H264 (bien formé) : 10 fps, 256 kbps, intra toutes les 10 trames.
		Properties props;
		H264Encoder enc(props);
		enc.SetFrameRate(FPS, 256, FPS);
		RET_FALSE_IF_NEG(enc.SetSize(W, H));

		for (int i = 0; i < NFRAMES; i++)
		{
			// Image I420 unie (variation de luma pour éviter une image figée).
			PictPtr pic = Pict::CreateColor(W, H, 16 + (i * 8) % 200, 128, 128);
			RET_FALSE_IF_NEG(pic ? 0 : -1);

			VideoFramePtr vf = enc.EncodeFrame(pic);
			if (vf)
			{
				vf->SetTimestamp((DWORD)(i * (90000 / FPS)));    // horloge 90 kHz
				if (w.ProcessFrame(vf.get()) == 1)
					videoWritten++;
			}

			// Une trame audio PCMU de 20 ms (160 échantillons µ-law = 0xFF silence).
			AudioFrame af(AudioCodec::PCMU, 8000);
			std::vector<BYTE> pcm(160, 0xFF);
			af.SetMedia(&pcm[0], pcm.size());
			af.SetTimestamp((DWORD)(i * 160));                   // horloge 8 kHz
			if (w.ProcessFrame(&af) == 1)
				audioWritten++;
		}
	} // <- destruction du mp4writer (MP4TagsStore) AVANT MP4Close

	MP4Close(mp4);
	return true;
}

} // namespace

TEST(Mp4RoundTrip, AudioPcmuVideoH264)
{
	int vw = 0, aw = 0;
	ASSERT_TRUE(WriteMp4(vw, aw)) << "écriture MP4 échouée";
	EXPECT_GT(vw, 0);
	EXPECT_GT(aw, 0);

	// Relecture
	Mp4FfReader r(PATH);
	ASSERT_TRUE(r.IsOpen());

	VideoCodec::Type vc[] = { VideoCodec::H264 };
	EXPECT_GT(r.OpenTrack(vc, 1, VideoCodec::H264, false, false), 0);
	AudioCodec::Type ac[] = { AudioCodec::PCMU };
	EXPECT_GT(r.OpenTrack(ac, 1, AudioCodec::PCMU, false), 0);

	EXPECT_TRUE(r.HasVideoTrack());
	EXPECT_TRUE(r.HasAudioTrack());

	VideoCodec::Type gotV; ASSERT_TRUE(r.GetVideoCodec(gotV));
	EXPECT_EQ(gotV, VideoCodec::H264);
	EXPECT_EQ(r.GetVideoWidth(),  (DWORD)W);
	EXPECT_EQ(r.GetVideoHeight(), (DWORD)H);

	AudioCodec::Type gotA; ASSERT_TRUE(r.GetCodec(gotA));
	EXPECT_EQ(gotA, AudioCodec::PCMU);

	// La vidéo produite par l'encodeur DOIT se relire (au moins une intra).
	int nVideo = 0, nAudio = 0, err = 0; unsigned long wait = 0;
	r.Rewind();
	for (int i = 0; i < 5000 && nVideo == 0; i++)
	{
		MediaFrame* f = r.GetNextFrame(err, wait);
		if (err == -1) break;                 // EOF
		ASSERT_GE(err, 0) << "GetNextFrame errcode=" << err;
		if (f && f->GetType() == MediaFrame::Video) nVideo++;
		else if (f && f->GetType() == MediaFrame::Audio) nAudio++;
		if (wait > 0) usleep(wait * 1000);    // respecter le cadencement
	}
	EXPECT_GT(nVideo, 0) << "aucune trame vidéo relue";

	unlink(PATH);
}
