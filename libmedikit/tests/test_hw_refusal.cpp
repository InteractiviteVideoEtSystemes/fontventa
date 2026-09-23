/**
 * test_hw_refusal.cpp — chemins GPU refusés par la sonde de démarrage du serveur
 * (VideoAccel::RefuseHw, lot 2 de la sonde du mediaserver).
 *
 * Un refus est définitif pour le processus : chaque test tourne dans un
 * sous-processus (EXPECT_EXIT), en style « threadsafe » : le binaire est
 * relancé, car un fork() d'un processus qui a déjà touché au GPU hérite d'un
 * état libva inutilisable. Les tests HwRefusal.DISABLED_* exigent un GPU et
 * vérifient d'abord, sans refus, que le chemin est bien matériel :
 *   ./tests/runtests --gtest_also_run_disabled_tests --gtest_filter='HwRefusal.*'
 */
#include <gtest/gtest.h>
#include <vector>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <medkit/videorescaler.h>
#include <h264/h264encoder.h>
#include <h264/h264decoder.h>

namespace {

const int W = 320, H = 240;

std::unique_ptr<H264Encoder> SoftwareEncoder(const char* plid)
{
	Properties props;
	props.SetProperty("video.hwaccel", "0");
	props.SetProperty("h264.profile-level-id", plid);
	std::unique_ptr<H264Encoder> enc(new H264Encoder(props));
	enc->SetFrameRate(25, 256, 25);
	enc->SetSize(W, H);
	return enc;
}

// Encode en logiciel, décode avec `dec` et compte les images sorties en surface
// GPU. spsFlags >= 0 réécrit l'octet de contraintes du SPS.
int GpuFrames(H264Decoder& dec, const char* plid, int spsFlags)
{
	std::unique_ptr<H264Encoder> enc = SoftwareEncoder(plid);
	int gpu = 0;
	for (int i = 0; i < 10; i++)
	{
		VideoFramePtr vf = enc->EncodeFrame(Pict::CreateColor(W, H, (BYTE)(40 + i * 10), 90, 160));
		if (!vf)
			continue;
		std::vector<BYTE> ab;
		BYTE* d = vf->GetData();
		DWORD len = vf->GetLength(), p = 0;
		while (p + 4 <= len)
		{
			DWORD n = (d[p] << 24) | (d[p+1] << 16) | (d[p+2] << 8) | d[p+3];
			if (p + 4 + n > len)
				break;
			size_t at = ab.size() + 4;
			ab.insert(ab.end(), { 0, 0, 0, 1 });
			ab.insert(ab.end(), d + p + 4, d + p + 4 + n);
			if (spsFlags >= 0 && n >= 4 && (ab[at] & 0x1f) == 7)
				ab[at + 2] = (BYTE)spsFlags;
			p += 4 + n;
		}
		size_t used = ab.size();
		ab.resize(used + AV_INPUT_BUFFER_PADDING_SIZE, 0);
		PictPtr before = dec.GetFrame();
		dec.Decode(ab.data(), used);
		PictPtr pic = dec.GetFrame();
		if (pic && pic != before && pic->IsGPUPict())
			gpu++;
	}
	return gpu;
}

bool HardwareEncoder()
{
	Properties props;
	H264Encoder enc(props);
	enc.SetFrameRate(25, 256, 25);
	return enc.SetSize(W, H) >= 1 && enc.IsHardwareReady();
}

int UnRefusNeVautQuePourSonChemin()
{
	bool before = VideoAccel::IsHwRefused("h264.encode");
	VideoAccel::RefuseHw("h264.encode");
	bool ok = !before
	       && VideoAccel::IsHwRefused("h264.encode")
	       && !VideoAccel::IsHwRefused("h264.decode")
	       && !VideoAccel::IsHwRefused("vp8.encode");
	return ok ? 0 : 1;
}

int UnEncodeurRefusePasseEnLogicielSansCompterDeRepli()
{
	if (!HardwareEncoder())
		return 2;	// pas de GPU : le test ne prouverait rien
	VideoAccel::RefuseHw("h264.encode");
	int fallbacks = VideoAccel::GetStats().hwFallbacks;
	Properties props;
	H264Encoder enc(props);
	enc.SetFrameRate(25, 256, 25);
	bool soft = enc.SetSize(W, H) >= 1 && !enc.IsHardwareReady();

	Properties required;
	required.SetProperty("video.hwaccel.required", "1");
	H264Encoder strict(required);
	strict.SetFrameRate(25, 256, 25);
	bool strictFails = strict.SetSize(W, H) < 1;

	bool counted = VideoAccel::GetStats().hwFallbacks != fallbacks;
	return !soft ? 3 : !strictFails ? 4 : counted ? 5 : 0;
}

int UnProfilRefuseSeDecodeEnLogiciel()
{
	{
		H264Decoder dec;
		if (GpuFrames(dec, "42801F", 0x80) < 5)
			return 2;
	}
	VideoAccel::RefuseHw("h264.decode.baseline");
	H264Decoder baseline, constrained;
	int refusedGpu = GpuFrames(baseline, "42801F", 0x80);
	int keptGpu    = GpuFrames(constrained, "42e01f", -1);
	return refusedGpu == 0 && keptGpu >= 5 ? 0 : 1;
}

int UnDecodeurRefusePasseEnLogiciel()
{
	{
		H264Decoder dec;
		if (!dec.IsHardwareReady())
			return 2;
	}
	VideoAccel::RefuseHw("h264.decode");
	H264Decoder dec;
	H264Decoder strict(/*requireHW*/ true);
	return !dec.IsHardwareReady() && !strict.IsHardwareReady() ? 0 : 1;
}

int UnRetaillageRefuseResteSurCpu()
{
	PictPtr gpu;
	if (Pict::CreateColor(W, H, 90, 128, 128)->UploadToGPU(gpu) < 0)
		return 2;
	{
		VideoRescaler r;
		PictPtr out = r.Rescale(gpu, W / 2, H / 2, false);
		if (!out || !out->IsGPUPict())
			return 2;
	}
	VideoAccel::RefuseHw("scale");
	VideoRescaler r;
	PictPtr out = r.Rescale(gpu, W / 2, H / 2, false);
	bool ok = out && !out->IsGPUPict()
	       && out->GetWidth() == (DWORD)(W / 2) && out->GetHeight() == (DWORD)(H / 2)
	       && out->GetAVFrame()->data[0][(H / 4) * out->GetAVFrame()->linesize[0] + W / 4] == 90;
	return ok ? 0 : 1;
}

} // namespace

TEST(HwRefusal, UnRefusNeVautQuePourSonChemin)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_EXIT(exit(UnRefusNeVautQuePourSonChemin()), ::testing::ExitedWithCode(0), "");
}

TEST(HwRefusal, DISABLED_UnEncodeurRefusePasseEnLogicielSansCompterDeRepli)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_EXIT(exit(UnEncodeurRefusePasseEnLogicielSansCompterDeRepli()), ::testing::ExitedWithCode(0), "");
}

TEST(HwRefusal, DISABLED_UnProfilRefuseSeDecodeEnLogiciel)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_EXIT(exit(UnProfilRefuseSeDecodeEnLogiciel()), ::testing::ExitedWithCode(0), "");
}

TEST(HwRefusal, DISABLED_UnDecodeurRefusePasseEnLogiciel)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_EXIT(exit(UnDecodeurRefusePasseEnLogiciel()), ::testing::ExitedWithCode(0), "");
}

TEST(HwRefusal, DISABLED_UnRetaillageRefuseResteSurCpu)
{
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	EXPECT_EXIT(exit(UnRetaillageRefuseResteSurCpu()), ::testing::ExitedWithCode(0), "");
}
