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

// Un terminal SIP annonce couramment du Baseline (42801f) sans en utiliser les
// outils. VAAPI ne décode que le Constrained Baseline : sans tolérance de
// profil, le décodeur passait en logiciel sans rien dire.
TEST(H264HwVaapi, DISABLED_UnFluxBaselineSeDecodeSurGpu)
{
	const int W = 320, H = 240;
	Properties props;
	props.SetProperty("video.hwaccel", "0");
	H264Encoder enc(props);
	enc.SetFrameRate(25, 256, 25);
	ASSERT_GE(enc.SetSize(W, H), 0);

	H264Decoder dec(/*requireHW*/ true);
	ASSERT_TRUE(dec.IsHardwareReady()) << "décodeur H264 VAAPI indisponible (pas de GPU ?)";

	int gpuFrames = 0, spsPatched = 0;
	for (int i = 0; i < 20; i++)
	{
		VideoFramePtr vf = enc.EncodeFrame(Pict::CreateColor(W, H, (BYTE)(40 + i * 5), 90, 160));
		if (!vf)
			continue;

		// AVCC -> Annex-B, en rabattant le SPS sur 42 80 : ce que déclare le terminal.
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
			if (n >= 4 && (ab[at] & 0x1f) == 7)
			{
				ab[at + 2] = 0x80;
				spsPatched++;
			}
			p += 4 + n;
		}
		size_t used = ab.size();
		ab.resize(used + AV_INPUT_BUFFER_PADDING_SIZE, 0);

		PictPtr before = dec.GetFrame();
		dec.Decode(ab.data(), used);
		PictPtr pic = dec.GetFrame();
		if (pic && pic != before && pic->IsGPUPict())
			gpuFrames++;
	}

	ASSERT_GT(spsPatched, 0) << "aucun SPS dans le flux";
	EXPECT_GE(gpuFrames, 15) << "flux 42801f décodé hors GPU";
}
