/**
 * test_mp4_transcode.cpp — transcodage complet + enregistrement 3GP.
 *
 * Lit titi.mp4 (H264 640x360 + AAC 44.1 kHz), transcode la vidéo H264 -> H263
 * (décodage H264 puis réencodage H263) et l'audio AAC -> AMR-NB
 * (Mp4FfReader::OpenAudioTranscoded), écrit le tout dans un fichier .3gp via
 * mp4writer, puis relit le 3GP et vérifie les pistes/codecs/dimensions.
 *
 * NB : les trames vidéo du reader sont en AVCC (préfixe de longueur) ; le
 * décodeur ffmpeg attend de l'Annex-B (start codes) -> conversion locale.
 * NB : nécessite un encodeur AMR-NB (libopencore_amrnb) et H263 côté ffmpeg.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/ffmp4reader.h>
#include <medkit/mp4writer.h>
#include <medkit/video.h>
#include <medkit/audio.h>
#include <h264/h264decoder.h>
#include <h263/h263codec.h>
#include <opus/opuscodec.h>
#include <mp4v2/mp4v2.h>
#include <vector>
#include <unistd.h>

#ifndef TEST_MP4_TITI_FILE
#define TEST_MP4_TITI_FILE "fixtures/titi.mp4"
#endif

namespace {

const char* OUT_3GP = "/tmp/libmedkit_titi.3gp";
const int   kTarget = 15; // trames de chaque type suffisant pour un test rapide

// AVCC (préfixe de longueur 4 octets) -> Annex-B (start code 00 00 00 01).
std::vector<BYTE> AvccToAnnexB(BYTE* d, DWORD L)
{
	std::vector<BYTE> o;
	DWORD p = 0;
	while (p + 4 <= L)
	{
		DWORD n = (d[p] << 24) | (d[p+1] << 16) | (d[p+2] << 8) | d[p+3];
		if (p + 4 + n > L) break;
		o.push_back(0); o.push_back(0); o.push_back(0); o.push_back(1);
		o.insert(o.end(), d + p + 4, d + p + 4 + n);
		p += 4 + n;
	}
	return o;
}

} // namespace

TEST(Mp4Transcode, H264versH263_AacversAmr_3gp)
{
	Mp4FfReader r(TEST_MP4_TITI_FILE);
	if (!r.IsOpen()) GTEST_SKIP() << "fixture absente : " << TEST_MP4_TITI_FILE;

	VideoCodec::Type vc[] = { VideoCodec::H264 };
	ASSERT_GT(r.OpenTrack(vc, 1, VideoCodec::H264, false, false), 0);
	ASSERT_EQ(r.OpenAudioTranscoded(AudioCodec::AMR), 1) << "transcodage AAC->AMR-NB indisponible";

	H264Decoder vdec;
	Properties props;
	H263Encoder venc(props);
	bool encOpened = false;

	MP4FileHandle mp4 = MP4Create(OUT_3GP, 0);
	ASSERT_NE(mp4, MP4_INVALID_FILE_HANDLE);

	int nv = 0, na = 0, err = 0;
	unsigned long wait = 0;
	{
		// Destruction du mp4writer AVANT MP4Close (cf. test_mp4_roundtrip).
		mp4writer w(NULL, mp4, /*waitVideo*/ false);
		w.EnableVideoPrologue(false);
		w.AddTrack(VideoCodec::H263_1998, 640, 360, 256, "video", false);
		w.AddTrack(AudioCodec::AMR, 8000, "audio");

		r.Rewind();
		for (int i = 0; i < 200000 && (nv < kTarget || na < kTarget); i++)
		{
			MediaFrame* f = r.GetNextFrame(err, wait);
			if (err == -1) break;                 // EOF
			ASSERT_GE(err, 0) << "GetNextFrame errcode=" << err;

			if (f && f->GetType() == MediaFrame::Video)
			{
				VideoFrame* vf = (VideoFrame*)f;
				std::vector<BYTE> ab = AvccToAnnexB(vf->GetData(), vf->GetLength());
				if (!ab.empty())
				{
					vdec.Decode(&ab[0], ab.size());
					PictPtr pic = vdec.GetFrame();
					// L'encodeur H263 est logiciel : une trame décodée en VAAPI
					// doit redescendre explicitement en CPU (aucun download
					// implicite côté décodeur, cf. avframe.md).
					if (pic && pic->IsGPUPict())
						pic = pic->DownloadToCPU();
					if (pic)
					{
						int dw = pic->GetWidth(), dh = pic->GetHeight();
						if (!encOpened)
						{
							venc.SetFrameRate(25, 256, 25);
							ASSERT_GE(venc.SetSize(dw, dh), 0) << "ouverture encodeur H263";
							encOpened = true;
						}
						VideoFrame* h = venc.EncodeFrame(pic);
						if (h)
						{
							h->SetTimestamp(vf->GetTimeStamp());
							if (w.ProcessFrame(h) == 1) nv++;
						}
					}
				}
			}
			else if (f && f->GetType() == MediaFrame::Audio)
			{
				if (w.ProcessFrame(f) == 1) na++;
			}
			if (wait > 0) usleep(wait * 1000);
		}
	}
	MP4Close(mp4);

	EXPECT_GE(nv, kTarget) << "trop peu de trames vidéo transcodées";
	EXPECT_GE(na, kTarget) << "trop peu de trames audio transcodées (AAC->AMR)";

	// Relecture du 3GP produit.
	Mp4FfReader r2(OUT_3GP);
	ASSERT_TRUE(r2.IsOpen());

	VideoCodec::Type vc2[] = { VideoCodec::H263_1998, VideoCodec::H263_1996 };
	EXPECT_GT(r2.OpenTrack(vc2, 2, VideoCodec::H263_1998, false, false), 0);
	AudioCodec::Type ac2[] = { AudioCodec::AMR };
	EXPECT_GT(r2.OpenTrack(ac2, 1, AudioCodec::AMR, false), 0);

	EXPECT_TRUE(r2.HasVideoTrack());
	EXPECT_TRUE(r2.HasAudioTrack());

	VideoCodec::Type gv;
	ASSERT_TRUE(r2.GetVideoCodec(gv));
	EXPECT_TRUE(gv == VideoCodec::H263_1996 || gv == VideoCodec::H263_1998);
	EXPECT_EQ(r2.GetVideoWidth(),  640u);
	EXPECT_EQ(r2.GetVideoHeight(), 360u);

	AudioCodec::Type ga;
	ASSERT_TRUE(r2.GetCodec(ga));
	EXPECT_EQ(ga, AudioCodec::AMR);

	unlink(OUT_3GP);
}

// AAC -> OPUS avec bornes négociées (phase 5) : trames de 20 ms exactement
// (960 ticks à 48 kHz) et bitstream décodable par le décodeur opus.
TEST(Mp4Transcode, AacVersOpusAvecBornes)
{
	Mp4FfReader r(TEST_MP4_TITI_FILE);
	if (!r.IsOpen()) GTEST_SKIP() << "fixture absente : " << TEST_MP4_TITI_FILE;

	Properties props;
	props["opus.useinbandfec"] = "1";
	ASSERT_EQ(r.OpenAudioTranscoded(AudioCodec::OPUS, props), 1) << "transcodage AAC->OPUS indisponible";

	AudioCodec::Type ac;
	ASSERT_TRUE(r.GetCodec(ac));
	EXPECT_EQ(ac, AudioCodec::OPUS);

	OPUSDecoder dec;
	SWORD pcm[8192];
	int na = 0, err = 0;
	unsigned long wait = 0;
	DWORD prevTs = 0;
	bool first = true;

	r.Rewind();
	for (int i = 0; i < 200000 && na < 50; i++)
	{
		MediaFrame* f = r.GetNextFrame(err, wait);
		if (err == -1) break;
		ASSERT_GE(err, 0) << "GetNextFrame errcode=" << err;

		if (f && f->GetType() == MediaFrame::Audio)
		{
			if (!first)
				EXPECT_EQ(f->GetTimeStamp() - prevTs, 960u);
			first = false;
			prevTs = f->GetTimeStamp();
			EXPECT_GT(dec.Decode(f->GetData(), (int)f->GetLength(), pcm,
			                     (int)(sizeof(pcm)/sizeof(pcm[0]))), 0);
			na++;
		}
		if (wait > 0) usleep(wait * 1000);
	}
	EXPECT_GE(na, 50) << "trop peu de trames audio transcodées (AAC->OPUS)";
}
