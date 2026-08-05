#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <utility>
#include "../medkit/log.h"
#include "../medkit/tools.h"
#include "h264encoder.h"

extern "C" {
#include <libavutil/opt.h>
}


extern std::string BuildH264Fmtp(int payloadType, const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);

//////////////////////////////////////////////////////////////////////////
//Encoder H264 (ffmpeg : h264_vaapi si dispo, libx264 sinon)
//////////////////////////////////////////////////////////////////////////

/**********************
* H264Encoder
*	Constructor de la clase
***********************/
H264Encoder::H264Encoder(const Properties& properties)
	: FfVideoEncoder(properties, AV_CODEC_ID_H264, VideoCodec::H264, /*tryHW*/ true)
{
	h264ProfileLevelId = properties.GetProperty("h264.profile-level-id",std::string("42801F"));
	intraRefresh = (bool) properties.GetProperty("h264.intra_refresh",0);
	qPel =  properties.GetProperty("h264.qpel",3);
	openedBitrate = 0;
	spsPpsCached = false;
}

/**********************
* ~H264Encoder
*	Destructor
***********************/
H264Encoder::~H264Encoder()
{
}

/**********************
* GetProfileLevel
*	Décode le profile-level-id SDP (ex 42801F), avec le même écrêtage de
*	level que l'ancien encodeur x264
***********************/
void H264Encoder::GetProfileLevel(int &profile, int &level)
{
	profile = strtol(h264ProfileLevelId.substr(0,2).c_str(),NULL,16);
	level   = strtol(h264ProfileLevelId.substr(4,2).c_str(),NULL,16);

	if (level < 9)
		// level choisi par l'encodeur selon le débit
		level = -1;
	else if (level >13 && level < 20)
		level = 13;
	else if (level > 22 && level < 30)
		level = 30;
	else if (level > 32 && level < 40)
		level = 32;
	else if (level > 42 && level < 50)
		level = 42;
	else if (level > 52)
		level = 52;
}

/**********************
* ConfigureContext
*	Réglages H264 du contexte ffmpeg, appelés par OpenCodec() juste avant
*	avcodec_open2() (le générique bit_rate/time_base/gop_size est déjà posé)
***********************/
void H264Encoder::ConfigureContext()
{
	// Un (ré)ouverture (resize, bascule VAAPI<->logiciel...) produit un
	// nouveau SPS : invalide le cache utilisé par GetFmtpInfo.
	spsPpsCached = false;
	cachedSps.clear();
	cachedPps.clear();

	int profile;
	int level;

	GetProfileLevel(profile, level);

	if (level > 0)
		ctx->level = level;

	if (IsHWAccelerated())
	{
		// --- Encodeur VAAPI ---
		// VBR piloté par bit_rate (moyenne) / rc_max_rate (crête). Pas de
		// reconfiguration en cours de route : SetFrameRate() rouvre le codec.
		ctx->bit_rate	    = bitrate*0.8;
		ctx->rc_max_rate    = bitrate;
		ctx->rc_buffer_size = bitrate;	// 1 seconde de VBV

		switch (profile)
		{
			case 100:
			case 88:
				ctx->profile = FF_PROFILE_H264_HIGH;
				break;
			case 77:
				ctx->profile = FF_PROFILE_H264_MAIN;
				break;
			case 66:
			default:
				// le baseline "pur" n'est pas supporté par la plupart
				// des drivers VAAPI
				ctx->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
				break;
		}
	}
	else
	{
		// --- libx264 ---
		// Mêmes réglages que l'ancien encodeur x264 direct (CRF borné VBV,
		// zerolatency, slices calibrées sur le payload RTP).
		av_opt_set(ctx->priv_data, "preset", "medium", 0);
		av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
		// FPU => vraie IDR (resynchronisation du décodeur distant)
		av_opt_set_int(ctx->priv_data, "forced-idr", 1, 0);
		// CRF 23 borné par le VBV : le débit suit rc_max_rate/rc_buffer_size,
		// que le wrapper libx264 relit à chaque trame => reconfigurable à
		// chaud sans réouverture (cf. SetFrameRate).
		av_opt_set_double(ctx->priv_data, "crf", 23, 0);
		ctx->rc_max_rate    = 0.6*bitrate;
		ctx->rc_buffer_size = ctx->rc_max_rate;

		switch (profile)
		{
			case 100:
			case 88:
				av_opt_set(ctx->priv_data, "profile", "high", 0);
				break;
			case 77:
				av_opt_set(ctx->priv_data, "profile", "main", 0);
				break;
			case 66:
			default:
				av_opt_set(ctx->priv_data, "profile", "baseline", 0);
				break;
		}

		char x264params[256];
		snprintf(x264params, sizeof(x264params),
			 "slice-max-size=%d:scenecut=0:subme=%d:ref=1%s",
			 RTPPAYLOADSIZE-8, qPel, intraRefresh ? ":intra-refresh=1" : "");
		av_opt_set(ctx->priv_data, "x264-params", x264params, 0);

		ctx->thread_count = 1;
	}

	Log("-H264Encoder: %s encoder, profile-level-id %s => profile %d level %d\n",
	    IsHWAccelerated() ? "VAAPI" : "software", h264ProfileLevelId.c_str(), profile, level);

	// Mémorise le débit d'ouverture pour la logique de réouverture VAAPI
	openedBitrate = bitrate;
}

/************************
* SetFrameRate
* 	Reconfiguration à chaud pour l'adaptation dynamique de débit. Appelé
* 	depuis la boucle d'encodage (même thread que EncodeFrame), potentiellement
* 	à chaque trame.
**************************/
int H264Encoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Mémorise fps/débit/période intra
	FfVideoEncoder::SetFrameRate(frames,kbits,intraPeriod);

	if (opened)
	{
		if (IsHWAccelerated())
		{
			// VAAPI ne sait pas changer son rate control en cours de route :
			// réouverture (=> nouvelle IDR), seulement sur une variation
			// significative (>=10%) pour ne pas rouvrir à chaque
			// micro-ajustement de la boucle d'adaptation (+8%/s).
			if (openedBitrate > 0 && abs(bitrate-openedBitrate)*10 >= openedBitrate)
				ReopenCodec();
		}
		else
		{
			// Le wrapper libx264 relit ces champs à chaque trame et applique
			// x264_encoder_reconfig() : mise à jour sans réouverture ni IDR.
			ctx->bit_rate	    = bitrate;
			ctx->rc_max_rate    = 0.6*bitrate;
			ctx->rc_buffer_size = ctx->rc_max_rate;
		}
	}

	return 1;
}

/**********************
* EncodeFrame
*	Codifica un frame
***********************/
VideoFrame* H264Encoder::EncodeFrame(PictPtr pic)
{
	// Patch IVeS : une I-frame toutes les 2 s pendant la première période
	// intra (repris de l'ancien encodeur x264)
	if (opened && fps > 0 && pts < 8*fps && (pts % (2*fps)) == 0)
		FastPictureUpdate();

	return FfVideoEncoder::EncodeFrame(pic);
}

/**********************
* PacketizeFrame
*	ffmpeg (libx264 comme h264_vaapi) produit un flux Annex-B (start codes).
*	Tout l'aval (packetisation RTP, descripteur AVC RTMP, enregistrement MP4)
*	attend des NALs préfixées par leur taille sur 4 octets, comme l'ancien
*	encodeur x264 (b_annexb=0) : on convertit, puis on réécrit dans chaque SPS
*	le profile-level-id négocié en SDP (interop), et on packetise
*	(single NAL / FU-A).
***********************/
void H264Encoder::PacketizeFrame()
{
	BYTE* data = frame->GetData();
	DWORD len  = frame->GetLength();

	// Découpe Annex-B : liste (offset,taille) des NALs
	std::vector< std::pair<DWORD,DWORD> > nalus;
	DWORD pos = 0;
	DWORD nalStart = 0;
	bool inNal = false;

	while (pos + 2 < len)
	{
		if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1)
		{
			if (inNal)
			{
				DWORD end = pos;
				// start code long (00 00 00 01) : le zéro précédent
				// appartient au start code, pas à la NAL
				if (end > nalStart && data[end-1] == 0)
					end--;
				nalus.push_back(std::make_pair(nalStart, end-nalStart));
			}
			pos += 3;
			nalStart = pos;
			inNal = true;
		}
		else
			pos++;
	}
	if (inNal && nalStart < len)
		nalus.push_back(std::make_pair(nalStart, len-nalStart));

	if (nalus.empty())
	{
		Error("-H264Encoder: no NAL found in encoded frame [len:%u]\n", len);
		return;
	}

	// profile-level-id négocié, réécrit dans chaque SPS
	DWORD profileLevel = strtol(h264ProfileLevelId.c_str(),NULL,16);

	// Reconstruit la trame en NALs préfixées taille 4 octets
	BYTE* out = (BYTE*)malloc(len + 4*nalus.size());
	DWORD outLen = 0;

	for (size_t i = 0; i < nalus.size(); ++i)
	{
		BYTE* nal     = data + nalus[i].first;
		DWORD nalSize = nalus[i].second;
		BYTE  nalType = nal[0] & 0x1f;

		// SPS : profile_idc/constraints/level_idc = valeur négociée
		if (nalSize >= 4 && nalType == 7)
			set3(nal,1,profileLevel);

		// Cache SPS/PPS pour GetFmtpInfo (une seule fois par ouverture,
		// invalidé par ConfigureContext au resize) : capturés APRÈS la
		// réécriture ci-dessus, pour refléter le profil réellement négocié.
		if (!spsPpsCached)
		{
			if (nalType == 7)
				cachedSps.assign(nal, nal + nalSize);
			else if (nalType == 8)
				cachedPps.assign(nal, nal + nalSize);
			if (!cachedSps.empty() && !cachedPps.empty())
				spsPpsCached = true;
		}

		// NB : le paramètre offset de set4() est un BYTE, on avance le pointeur
		set4(out+outLen,0,nalSize);
		memcpy(out+outLen+4,nal,nalSize);
		outLen += 4+nalSize;
	}

	frame->SetMedia(out,outLen);
	free(out);

	// Packetisation RTP single NAL / FU-A sur les préfixes de taille
	frame->SetH264NalSizeLength(4);
	frame->Packetize(RTPPAYLOADSIZE-2);
}

bool H264Encoder::GetFmtpInfo(std::string &fmtp, int payloadType)
{
	if (!spsPpsCached) return false;

	fmtp = BuildH264Fmtp(payloadType, cachedSps, cachedPps);
	return !fmtp.empty();
}

namespace
{

// Passe une chaîne en minuscules (les valeurs hexa du profile-level-id et les noms
// de paramètres SDP sont insensibles à la casse).
std::string LowerCopy(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
	return s;
}

// Un profile-level-id vaut exactement 6 chiffres hexadécimaux :
// profile_idc (2) || profile_iop (2) || level_idc (2). On rend le profil (les 4
// premiers, gardés tels quels puisque la règle 1 les recopie) et le niveau.
bool ParseProfileLevelId(const std::string& plid, std::string& profilePart, int& level)
{
	if (plid.size() != 6)
		return false;
	for (char c : plid)
		if (!isxdigit((unsigned char) c))
			return false;

	profilePart = plid.substr(0,4);
	level = (int) strtol(plid.substr(4,2).c_str(), NULL, 16);
	return true;
}

std::string FormatProfileLevelId(const std::string& profilePart, int level)
{
	char buf[8];
	snprintf(buf, sizeof(buf), "%s%02x", profilePart.c_str(), level & 0xFF);
	return LowerCopy(std::string(buf));
}

} // namespace

std::string H264Encoder::GetFmtpParams(const Properties& properties)
{
	// profile-level-id = notre capacité de décodage annoncée — la config, ou le
	// résultat de ResolveNegotiation quand un fmtp distant a été ingéré. En
	// minuscules par convention SDP (l'attribut est insensible à la casse).
	std::string profileLevelId = LowerCopy(properties.GetProperty("h264.profile-level-id", std::string("42801F")));

	// level-asymmetry-allowed=1 : un mixeur transcode dans les deux sens, donc le
	// cas même pour lequel ce paramètre existe — décoder à un niveau et encoder à un
	// autre — est le nôtre. Sans lui, RFC 6184 §8.2.2 nous imposerait le niveau de
	// l'offre même quand nous savons faire mieux.
	//
	// packetization-mode ANNONCÉ = le nôtre (mode 1). Le mode d'ÉMISSION, qui doit
	// être un mode que le pair a déclaré, voyage dans effectiveProps et pas ici.
	//
	// sprop-parameter-sets délibérément absent (cf. déclaration).
	return "profile-level-id=" + profileLevelId +
	       ";packetization-mode=1;level-asymmetry-allowed=1";
}

void H264Encoder::ResolveNegotiation(const Properties& localProps,
                                     const std::map<std::string,std::string>& remoteParams,
                                     Properties& announceProps,
                                     Properties& effectiveProps)
{
	// On repart des props locales dans les deux cas : tout ce que la négociation ne
	// touche pas (débit, intra_refresh, qpel…) doit survivre de part et d'autre.
	// NB : on écrit ensuite via operator[] et non SetProperty(), qui fait un insert
	// et n'écraserait donc PAS la valeur héritée.
	announceProps  = localProps;
	effectiveProps = localProps;

	const std::string localPlid = localProps.GetProperty("h264.profile-level-id", std::string("42801F"));

	std::string ourProfile;
	int ourLevel = 0;
	if (!ParseProfileLevelId(LowerCopy(localPlid), ourProfile, ourLevel))
	{
		// Notre propre config est illisible : il n'y a rien à résoudre contre elle.
		Log("-H264 ResolveNegotiation: local profile-level-id illisible [%s], negotiation skipped\n",
		    localPlid.c_str());
		return;
	}

	// Pas d'entrée distante — offer sortant, ou pair qui n'envoie aucun fmtp : on
	// annonce notre config telle quelle et rien ne borne l'encodeur au-delà d'elle.
	std::map<std::string,std::string>::const_iterator itPlid = remoteParams.find("profile-level-id");
	if (itPlid == remoteParams.end())
		return;

	std::string peerProfile;
	int peerLevel = 0;
	if (!ParseProfileLevelId(LowerCopy(itPlid->second), peerProfile, peerLevel))
	{
		Log("-H264 ResolveNegotiation: remote profile-level-id illisible [%s], keeping ours\n",
		    itPlid->second.c_str());
		return;
	}

	// RFC 6184 §8.2.2 : l'asymétrie de niveau exige level-asymmetry-allowed=1 des
	// DEUX côtés. Nous l'annonçons toujours (GetFmtpParams), donc seul le pair
	// tranche ici.
	std::map<std::string,std::string>::const_iterator itAsym = remoteParams.find("level-asymmetry-allowed");
	const bool peerAllowsAsymmetry = (itAsym != remoteParams.end() && itAsym->second == "1");

	int announcedLevel;
	if (peerAllowsAsymmetry)
	{
		// Asymétrie permise : nous annonçons notre vraie capacité de décodage.
		announcedLevel = ourLevel;
	}
	else if (peerLevel > ourLevel)
	{
		// Écart assumé à la RFC (qui ne laisse que refléter ou retirer) : refuser la
		// vidéo est un échec plus dur qu'annoncer en dessous, et annoncer en dessous
		// est justement ce dont un pair correct a besoin pour encoder à notre portée.
		// Ce log est la SEULE trace qui relie « pas de vidéo sur cette patte » à sa
		// cause : il doit nommer les deux niveaux.
		Log("-H264 ResolveNegotiation: offered level not decodable, announcing ours "
		    "[offered=0x%02x announced=0x%02x profile=%s]\n",
		    peerLevel, ourLevel, peerProfile.c_str());
		announcedLevel = ourLevel;
	}
	else
	{
		// Pas d'asymétrie et niveau décodable : règles 1 et 2 combinées ⇒ on renvoie
		// le profile-level-id de l'offre tel quel.
		announcedLevel = peerLevel;
	}

	// Règle 1 : le profil annoncé est celui de l'offre — nous n'annonçons jamais un
	// profil que l'appelant n'a pas nommé.
	announceProps["h264.profile-level-id"] = FormatProfileLevelId(peerProfile, announcedLevel);

	// Ce qui borne l'encodeur : le pair a déclaré ce qu'il sait décoder, et émettre
	// au-dessus produit un flux négocié avec succès et décodé par personne.
	effectiveProps["h264.profile-level-id"] =
		FormatProfileLevelId(peerProfile, peerLevel < ourLevel ? peerLevel : ourLevel);

	// packetization-mode n'est PAS régi par la règle d'asymétrie : c'est une capacité
	// de réception par direction. Ce que nous annonçons reste le nôtre ; ce que nous
	// émettons doit être un mode que le pair a déclaré, remonté ici en propriété. Le
	// packetiseur reste à y brancher (il émet aujourd'hui du single-NAL/FU-A, soit
	// le mode 1).
	std::map<std::string,std::string>::const_iterator itMode = remoteParams.find("packetization-mode");
	if (itMode != remoteParams.end())
		effectiveProps["h264.packetization-mode"] = itMode->second;
}