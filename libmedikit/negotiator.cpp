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

namespace
{

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

// fmtp local (params seuls) d'un codec vidéo. H264/VP8/AV1 en produisent.
std::string VideoFmtp(int codec, const Properties& props)
{
	switch ((VideoCodec::Type)codec)
	{
		case VideoCodec::H264: return H264Encoder::GetFmtpParams(props);
		case VideoCodec::VP8:  return VP8Encoder::GetFmtpParams(props);
		case VideoCodec::AV1:  return AV1Encoder::GetFmtpParams(props);
		default:               return "";
	}
}

} // namespace

bool CodecNegotiator::Negotiate(MediaFrame::Type media,
                                const std::map<int,int>& proposed,
                                const Properties& localProps,
                                const Properties* remoteFmtp,
                                NegotiationResult& out)
{
	// remoteFmtp : réservé à la négociation entrante (phase 5), ignoré ici.
	(void) remoteFmtp;

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
		nc.effectiveProps = localProps; // remoteFmtp ignoré en phase 3

		switch (media)
		{
			case MediaFrame::Audio:
				nc.fmtp = AudioFmtp(codec, localProps);
				break;
			case MediaFrame::Video:
				nc.fmtp = VideoFmtp(codec, localProps);
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
