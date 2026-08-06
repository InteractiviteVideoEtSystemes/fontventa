/**
 * negotiator.cpp — implémentation du négociateur de codecs (phase 3 nego_fmtp).
 *
 * Voir medkit/negotiator.h pour le rôle. Ce fichier ne fait que :
 *   - dispatcher IsSupported / GetFmtpParams par média + Type vers le catalogue
 *     de capacités (codecs.h) et les statiques d'encodeurs de la phase 2 ;
 *   - filtrer la RTPMap proposée et remplir NegotiationResult.
 * Aucun codec n'est ouvert (dérivation depuis Properties uniquement, §3.2).
 */
#include "medkit/negotiator.h"

// Statiques fmtp « params seuls » (phase 2), une par codec porteur de fmtp.
#include "opus/opuscodec.h"     // OPUSEncoder::GetFmtpParams
#include "h264/h264encoder.h"   // H264Encoder::GetFmtpParams
#include "vp8/vp8encoder.h"     // VP8Encoder::GetFmtpParams
#include "av1/av1codec.h"       // AV1Encoder::GetFmtpParams

std::map<std::string,std::string> ParseFmtpParams(const std::string& params)
{
	std::map<std::string,std::string> out;

	size_t pos = 0;
	while (pos <= params.size())
	{
		size_t sep = params.find(';', pos);
		std::string item = params.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);

		// Découpe clé/valeur, en tolérant un drapeau nu (pas de '=').
		size_t eq = item.find('=');
		std::string key = (eq == std::string::npos) ? item : item.substr(0, eq);
		std::string val = (eq == std::string::npos) ? std::string() : item.substr(eq + 1);

		// Espaces autour, et clé en minuscules (noms de paramètres SDP
		// insensibles à la casse).
		const char* ws = " \t\r\n";
		size_t b = key.find_first_not_of(ws), e = key.find_last_not_of(ws);
		key = (b == std::string::npos) ? std::string() : key.substr(b, e - b + 1);
		b = val.find_first_not_of(ws); e = val.find_last_not_of(ws);
		val = (b == std::string::npos) ? std::string() : val.substr(b, e - b + 1);
		for (char &c : key)
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';

		if (!key.empty())
			out[key] = val;

		if (sep == std::string::npos)
			break;
		pos = sep + 1;
	}

	return out;
}

namespace
{

// fmtp du pair pour UN PAYLOAD TYPE, découpé. Deux clés, dans cet ordre (§5.3 de
// nego_fmtp.md) :
//
//   "pt.<pt>.fmtp"   — par payload type. C'est la seule clé correcte quand un même
//                      codec est offert sous plusieurs PT, et un navigateur le fait
//                      systématiquement : Chrome énumère H.264 sous six ou sept PT
//                      pour décrire autant de couples (profil, packetization-mode).
//   "<nomcodec>.fmtp" — par nom de codec, la convention historique. Conservée pour
//                      le JSR-309, qui n'a qu'un PT par codec et pousse
//                      `codec.h264.fmtp` via SetRTPProperties.
//
// Map vide si le contrôleur n'a rien transmis pour ce PT ni pour son codec : le
// négociateur annonce alors notre propre config.
//
// **Le bug que la clé par PT corrige** (2026-08-06) : la clé par nom de codec seule
// force UNE résolution pour tous les PT d'un même codec. Sur une offre navigateur,
// `RTPParticipant::StartReceiving` écrasait `h264.fmtp` à chaque tour de boucle, le
// dernier PT gagnait, et les sept PT acceptés repartaient avec SON profil — donc six
// réponses décrivant un codec que l'appelant n'avait pas offert, et un navigateur qui
// refuse la réponse entière.
std::map<std::string,std::string> RemoteParamsFor(const Properties* remoteFmtp,
                                                  MediaFrame::Type media, int codec,
                                                  int pt)
{
	if (!remoteFmtp)
		return std::map<std::string,std::string>();

	char ptKey[32];
	snprintf(ptKey, sizeof(ptKey), "pt.%d.fmtp", pt);

	if (remoteFmtp->HasProperty(std::string(ptKey)))
		return ParseFmtpParams(remoteFmtp->GetProperty(std::string(ptKey), std::string()));

	const char* name = GetNameForCodec(media, (DWORD) codec);
	if (!name)
		return std::map<std::string,std::string>();

	std::string key(name);
	for (char &c : key)
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
	key += ".fmtp";

	if (!remoteFmtp->HasProperty(key))
		return std::map<std::string,std::string>();

	return ParseFmtpParams(remoteFmtp->GetProperty(key, std::string()));
}

bool IsCodecSupported(MediaFrame::Type media, int codec)
{
	switch (media)
	{
		case MediaFrame::Audio: return AudioCodec::IsSupported((AudioCodec::Type)codec);
		case MediaFrame::Video: return VideoCodec::IsSupported((VideoCodec::Type)codec);
		case MediaFrame::Text:  return TextCodec::IsSupported((TextCodec::Type)codec);
		default:                return false;
	}
}

// fmtp local (params seuls) d'un codec audio. Opus porte ses paramètres ;
// telephone-event (RFC 4733) porte sa plage de tonalités DTMF acceptées. Les
// autres codecs média (PCMU/PCMA/G722/GSM/AAC/AMR/Speex/Nelly) n'ont pas de fmtp
// -> chaîne vide (présents dans la struct avec valeur "", cf. contrat §5.2).
std::string AudioFmtp(int codec, const Properties& props)
{
	switch ((AudioCodec::Type)codec)
	{
		case AudioCodec::OPUS: return OPUSEncoder::GetFmtpParams(props);
		// Plage DTMF acceptée par le serveur (convention Asterisk : 0-9, *, #,
		// A-D + flash-hook). Le serveur est autoritatif sur cette plage.
		case AudioCodec::TELEPHONE_EVENT: return "0-16";
		default:               return "";
	}
}

// Résout un codec vidéo POUR UN PAYLOAD TYPE : le fmtp que nous ANNONÇONS (notre
// capacité de réception sur ce PT) et, dans `nc.effectiveProps`, ce qui BORNE NOTRE
// ENCODEUR (ce que le pair sait décoder sur ce PT). Les deux ne coïncident que
// lorsqu'aucun fmtp distant n'a été transmis.
//
// La résolution est bien par PT et non par codec : sur une offre navigateur, le même
// H.264 arrive sous plusieurs PT qui décrivent des configurations différentes, et
// chacune doit être répondue avec la sienne (RFC 6184 §8.2.2).
//
// Seul H.264 ingère le fmtp distant pour l'instant (RFC 6184 §8.2.2, cf.
// H264Encoder::ResolveNegotiation). VP8 et AV1 dérivent encore leur fmtp de la
// seule config locale : l'ingestion AV1 (profile / level-idx / tier) est à
// spécifier avant d'être écrite, et VP8 n'a pas de paramètre qui s'y prête.
void ResolveVideo(int codec, const Properties& localProps,
                  const std::map<std::string,std::string>& remoteParams,
                  NegotiatedCodec& nc)
{
	switch ((VideoCodec::Type)codec)
	{
		case VideoCodec::H264:
		{
			Properties announceProps;
			H264Encoder::ResolveNegotiation(localProps, remoteParams, announceProps, nc.effectiveProps);
			nc.fmtp = H264Encoder::GetFmtpParams(announceProps);
			break;
		}
		case VideoCodec::VP8:
			nc.fmtp = VP8Encoder::GetFmtpParams(localProps);
			break;
		case VideoCodec::AV1:
			nc.fmtp = AV1Encoder::GetFmtpParams(localProps);
			break;
		default:
			nc.fmtp = "";
			break;
	}
}

} // namespace

bool CodecNegotiator::Negotiate(MediaFrame::Type media,
                                const std::map<int,int>& proposed,
                                const Properties& localProps,
                                const Properties* remoteFmtp,
                                NegotiationResult& out)
{
	out.acceptedMap.clear();
	out.codecs.clear();

	if (media != MediaFrame::Audio &&
	    media != MediaFrame::Video &&
	    media != MediaFrame::Text)
		return false;

	// Le fmtp du T140RED (RFC 4103) référence le PT du T140 primaire : le
	// localiser dans la map proposée (s'il est offert et supporté). Sans T140
	// companion, le RED n'a pas de paramètre exploitable -> fmtp vide.
	int t140Pt = -1;
	if (media == MediaFrame::Text)
	{
		for (const auto& kv : proposed)
		{
			if (kv.second == TextCodec::T140 && IsCodecSupported(media, kv.second))
			{
				t140Pt = kv.first;
				break;
			}
		}
	}

	// Intersection supportés ∩ proposés, dans l'ordre des PT proposés.
	for (const auto& kv : proposed)
	{
		const int pt    = kv.first;
		const int codec = kv.second;

		// PT non supporté -> il DISPARAÎT de la map acceptée (décision D).
		if (!IsCodecSupported(media, codec))
			continue;

		NegotiatedCodec nc;
		nc.payloadType   = pt;
		nc.codec         = codec;
		// Par défaut l'encodeur n'est borné que par notre config ; les codecs qui
		// ingèrent le fmtp distant écrasent ceci (ResolveVideo).
		nc.effectiveProps = localProps;

		switch (media)
		{
			case MediaFrame::Audio:
				nc.fmtp = AudioFmtp(codec, localProps);
				break;
			case MediaFrame::Video:
				ResolveVideo(codec, localProps, RemoteParamsFor(remoteFmtp, media, codec, pt), nc);
				break;
			case MediaFrame::Text:
				if ((TextCodec::Type)codec == TextCodec::T140RED && t140Pt >= 0)
					nc.fmtp = TextCodec::GetT140RedFmtpParams(t140Pt);
				else
					nc.fmtp = "";
				break;
			default:
				nc.fmtp = "";
				break;
		}

		out.acceptedMap[pt] = codec;
		out.codecs.push_back(nc);
	}

	return true;
}
