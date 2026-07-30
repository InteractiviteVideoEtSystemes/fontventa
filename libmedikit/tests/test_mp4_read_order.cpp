/**
 * test_mp4_read_order.cpp — ordre d'émission de Mp4FfReader.
 *
 * mp4v2 interleave le fichier par tranches (~1 s de vidéo, puis ~1 s d'audio) ;
 * av_read_frame restitue cet ordre de stockage, pas l'ordre dts global. Un
 * lecteur qui cadence sur un seul paquet d'avance voit donc chaque tranche
 * « déjà due » et l'émet en rafale.
 *
 * Conséquence observée en production (mp4play, pcap) : Asterisk horodate les
 * trames vidéo sortantes à l'instant d'émission, donc une rafale colle une
 * vingtaine d'unités d'accès sur un même timestamp RTP — 24 bits de marqueur
 * pour un seul ts — et le pair ne décode plus l'IDR.
 *
 * Invariant testé : l'échéance de lecture (Tell()) ne recule jamais d'une trame
 * émise à la suivante, toutes pistes confondues.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <mp4v2/mp4v2.h>
#include <medkit/mp4writer.h>
#include <medkit/ffmp4reader.h>
#include <medkit/video.h>
#include <medkit/audio.h>
#include <h264/h264encoder.h>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace {

const char* PATH = "/tmp/libmedkit_readorder.mp4";
const int   W = 176, H = 144, FPS = 25;
// Durée > 1 s : il faut au moins deux tranches d'interleaving pour que le
// défaut se manifeste.
const int   NVIDEO   = 40;                 // 40 x 40 ms = 1,6 s
const DWORD FRAME_TS = 40 * 90;

bool WriteInterleavedMp4()
{
	MP4FileHandle mp4 = MP4Create(PATH, 0);
	if (mp4 == MP4_INVALID_FILE_HANDLE) return false;

	{
		mp4writer w(NULL, mp4, /*waitVideo*/ false);
		w.EnableVideoPrologue(false);          // on teste la lecture, pas le prologue
		w.AddTrack(VideoCodec::H264, W, H, 256, "video", false);

		Properties props;
		H264Encoder enc(props);
		enc.SetFrameRate(FPS, 256, FPS);
		if (enc.SetSize(W, H) < 0) { MP4Close(mp4); return false; }

		std::vector<BYTE> yuv(W * H * 3 / 2);
		std::vector<BYTE> pcm(160, 0xFF);

		for (int i = 0; i < NVIDEO; i++)
		{
			memset(&yuv[0], 16 + (i * 8) % 200, W * H);
			memset(&yuv[W * H], 128, W * H / 2);

			VideoFrame* vf = enc.EncodeFrame(&yuv[0], yuv.size());
			if (vf)
			{
				vf->SetTimestamp(i * FRAME_TS);            // horloge 90 kHz
				w.ProcessFrame(vf);
			}

			// 2 trames audio de 20 ms par trame vidéo de 40 ms
			for (int k = 0; k < 2; k++)
			{
				AudioFrame af(AudioCodec::PCMU, 8000);
				af.SetMedia(&pcm[0], pcm.size());
				af.SetTimestamp((i * 2 + k) * 20);         // horloge ms
				w.ProcessFrame(&af);
			}
		}
	} // <- destruction du writer AVANT MP4Close

	MP4Close(mp4);
	return true;
}

} // namespace

TEST(Mp4ReadOrder, EcheanceJamaisEnArriere)
{
	ASSERT_TRUE(WriteInterleavedMp4()) << "écriture MP4 échouée";

	Mp4FfReader r(PATH);
	ASSERT_TRUE(r.IsOpen());

	AudioCodec::Type ac[] = { AudioCodec::PCMU };
	VideoCodec::Type vc[] = { VideoCodec::H264 };
	ASSERT_GT(r.OpenTrack(ac, 1, AudioCodec::PCMU, false), 0);
	ASSERT_GT(r.OpenTrack(vc, 1, VideoCodec::H264, false, false), 0);
	r.Rewind();

	QWORD prev = 0;
	int nvideo = 0, naudio = 0, recul = 0;
	QWORD piredRecul = 0;

	for (;;)
	{
		int errcode = 0; unsigned long wait = 0;
		MediaFrame* f = r.GetNextFrame(errcode, wait);

		if (errcode == -1) break;                  // EOF
		if (errcode < 0) FAIL() << "GetNextFrame errcode=" << errcode;

		if (f)
		{
			QWORD now = r.Tell();
			if (now < prev)
			{
				recul++;
				if (prev - now > piredRecul) piredRecul = prev - now;
			}
			prev = now;

			if (f->GetType() == MediaFrame::Video)      nvideo++;
			else if (f->GetType() == MediaFrame::Audio) naudio++;
		}

		if (wait > 0) usleep(wait * 1000);
	}

	EXPECT_GT(nvideo, 0);
	EXPECT_GT(naudio, 0);
	EXPECT_EQ(recul, 0)
		<< recul << " retours en arrière de l'échéance (pire : " << piredRecul
		<< " ms) : les paquets ne sont pas réordonnés par dts, la lecture émet"
		   " des rafales";

	unlink(PATH);
}
