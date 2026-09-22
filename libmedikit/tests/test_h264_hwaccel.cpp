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

	// --- Aller-retour matériel : encode plusieurs images, décode-les ----------
	// PLUSIEURS, et c'est le fond du test : un encodeur matériel a une latence
	// (une image ici), donc le premier envoi ne rend rien et n'est pas un échec.
	// Attendre une trame du premier appel, comme faisait ce test, ne mesurait que
	// cette latence.
	//
	// Ce que ce test garde vraiment : que les surfaces VAAPI soient allouées en
	// NV12 (AllocateVAAPIFrame). En YUV420P, le driver refuse chaque image sans
	// le dire — avcodec_send_frame rend 0, aucun paquet ne sort JAMAIS, et le DPB
	// finit par déborder sur une assertion qui tue le processus. Une seule image
	// encodée ne l'aurait pas vu ; vingt le voient.
	const int kFrames = 20;
	int encoded = 0, decoded = 0;

	for (int i = 0; i < kFrames; i++)
	{
		PictPtr pic = Pict::CreateColor(W, H, (BYTE)(16 + (i * 7) % 200), 128, 128);
		ASSERT_TRUE(pic != nullptr);

		VideoFramePtr vf = enc.EncodeFrame(pic);
		if (!vf)
			continue;	// image encore dans l'encodeur, pas une erreur

		encoded++;
		if (dec.Decode(vf->GetData(), vf->GetLength()) >= 0)
			decoded++;
	}

	// La latence coûte au plus quelques images : en rendre zéro sur vingt est le
	// symptôme exact d'un format de surface que le driver n'encode pas.
	EXPECT_GE(encoded, kFrames - 4) << "encodage materiel muet ou intermittent";
	EXPECT_GE(decoded, encoded - 1) << "decodage materiel en echec";
}
