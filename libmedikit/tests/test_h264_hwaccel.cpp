/**
 * test_h264_hwaccel.cpp — accélération matérielle VAAPI (encodage ET décodage).
 *
 * DÉSACTIVÉ PAR DÉFAUT (préfixe gtest « DISABLED_ ») : ce test n'aboutit que sur
 * une machine disposant d'un GPU exposé via VAAPI (/dev/dri/renderD128). Sur une
 * machine sans GPU il ÉCHOUE volontairement — l'encodeur et le décodeur H264
 * sont configurés en mode « accélération matérielle EXIGÉE » (aucun repli
 * logiciel), donc leur ouverture échoue faute de device VAAPI.
 *
 * Le lancer explicitement :
 *   ./tests/runtests --gtest_also_run_disabled_tests --gtest_filter='*H264HwVaapi*'
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/config.h>
#include <medkit/video.h>
#include <h264/h264encoder.h>
#include <h264/h264decoder.h>
#include <vector>

TEST(H264HwVaapi, DISABLED_EncodeDecodeRequiresVaapi)
{
	const int W = 176, H = 144;

	// --- Encodeur H264 : accélération matérielle EXIGÉE -----------------------
	Properties props;
	props.SetProperty("video.hwaccel.required", "1");
	H264Encoder enc(props);
	enc.SetFrameRate(25, 256, 25);

	// Sans device VAAPI, l'ouverture de l'encodeur matériel échoue (pas de repli).
	ASSERT_GE(enc.SetSize(W, H), 0) << "encodeur H264 VAAPI indisponible (pas de GPU ?)";
	ASSERT_TRUE(enc.IsHardwareReady()) << "encodeur H264 non matériel";

	// --- Décodeur H264 : accélération matérielle EXIGÉE -----------------------
	H264Decoder dec(/*requireHW*/ true);
	ASSERT_TRUE(dec.IsHardwareReady()) << "décodeur H264 VAAPI indisponible (pas de GPU ?)";

	// --- Aller-retour matériel : encode une image, décode-la ------------------
	std::vector<BYTE> yuv(W * H * 3 / 2, 128); // image grise I420
	VideoFrame* vf = enc.EncodeFrame(&yuv[0], yuv.size());
	ASSERT_TRUE(vf != NULL) << "échec encodage matériel";

	int r = dec.Decode(vf->GetData(), vf->GetLength());
	ASSERT_GE(r, 0) << "échec décodage matériel";
}
