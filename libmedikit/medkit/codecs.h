#ifndef _CODECS_H_
#define _CODECS_H_

#include "config.h"
#include "media.h"
#include <map>
#include <vector>

class AudioCodec
{
public:
	enum Type {PCMA=8,PCMU=0,GSM=3,G722=9,SPEEX16=117,AMR=118,TELEPHONE_EVENT=100,NELLY8=130,NELLY11=131,OPUS=98,AAC=97,SLIN=99,AMRWB=120};
	static const char* GetNameFor(Type codec)
	{
		switch (codec)
		{
			case PCMA:	return "PCMA";
			case PCMU:	return "PCMU";
			case GSM:	return "GSM";
			case SPEEX16:	return "SPEEX16";
			case NELLY8:	return "NELLY8Khz";
			case NELLY11:	return "NELLY11Khz";
			case OPUS:	return "OPUS";
			case G722:	return "G722";
			case AAC:	return "AAC";
			case AMR:	return "AMR";
			case AMRWB:	return "AMR-WB";
			case TELEPHONE_EVENT: return "TELEPHONE_EVENT";
			default:	return "unknown";
		}
	}

	static bool GetCodecFor(const char * name, Type & c)
	{
	    if (name == NULL) return false;

	    if ( strcmp(name, "PCMA") == 0 || strcmp(name, "alaw") == 0)
	    {
		c = PCMA;
		return true;
	    }

	    if ( strcmp(name, "PCMU") == 0 || strcmp(name, "ulaw") == 0)
	    {
		c = PCMU;
		return true;
	    }

	    if ( strcmp(name, "AAC") == 0)
	    {
		c = AAC;
		return true;
	    }

	    if ( strcmp(name, "OPUS") == 0)
	    {
		c = OPUS;
		return true;
	    }


	    if ( strcmp(name, "AMR") == 0)
	    {
		c = AMR;
		return true;
	    }

	    if ( strcmp(name, "AMR-WB") == 0 || strcmp(name, "AMRWB") == 0)
	    {
		c = AMRWB;
		return true;
	    }

	    return false;
	}

	// Vrai si le média serveur (libmedikit/ffmpeg) sait réellement traiter ce
	// codec, c.-à-d. si le décodeur correspondant est disponible (même test
	// avcodec_find_decoder[_by_name] que l'ouverture réelle des classes Ff*).
	// Défini hors ligne dans codecs.cpp pour garder ce header sans ffmpeg.
	static bool IsSupported(Type codec);

	typedef std::map<int,Type> RTPMap;
};

class VideoCodec
{
public:
	enum Type {H263_1996=34,H263_1998=103,MPEG4=104,H264=99,SORENSON=100,VP6=106,VP8=107,ULPFEC=108,RED=109,AV1=110};
	static const char* GetNameFor(Type type)
	{
		switch (type)
		{
			case H263_1996:	return "H263_1996";
			case H263_1998:	return "H263_1998";
			case MPEG4:	return "MPEG4";
			case H264:	return "H264";
			case SORENSON:  return "SORENSON";
			case VP6:	return "VP6";
			case VP8:	return "VP8";
			case AV1:	return "AV1";
			default:	return "unknown";
		}
	}

	static bool GetCodecFor(const char * name, Type & c)
	{
	    if (name == NULL) return false;

	    if ( strcmp(name, "H264") == 0)
	    {
		c = H264;
		return true;
	    }

	    if ( strcmp(name, "H263_1996") == 0
	         ||
		 strcmp(name, "H263") == 0
		 ||
		 strcmp(name, "H263-1996") == 0)
	    {
			c = H263_1996;
			return true;
	    }

	    if ( strcmp(name, "H263_1998") == 0
	         ||
		 strcmp(name, "H263P") == 0
		 ||
		 strcmp(name, "H263-1998") == 0
		 ||
		 strcmp(name, "H263-2000") == 0)
	    {
		c = H263_1998;
		return true;
	    }

	    if ( strcmp(name, "VP8") == 0)
	    {
			c = VP8;
			return true;
	    }

	    if ( strcmp(name, "AV1") == 0)
	    {
			c = AV1;
			return true;
	    }

	    return false;
	}

	// cf. AudioCodec::IsSupported.
	static bool IsSupported(Type codec);

	typedef std::map<int,Type> RTPMap;

private:
};


class TextCodec
{
public:
	enum Type {T140=106,T140RED=105};
	static const char* GetNameFor(Type type)
	{
		switch (type)
		{
			case T140:	return "T140";
			case T140RED:	return "T140RED";
			default:	return "unknown";
		}
	}

	// T140/T140RED sont gérés nativement (pas de dépendance ffmpeg).
	static bool IsSupported(Type codec);

	// Paramètres fmtp du RED texte (RFC 4103), SANS "a=fmtp:<red_pt> " : la liste
	// des PT redondants "<t140_pt>/<t140_pt>/..." (generations éléments, primaire
	// inclus). Le contrôleur SIP préfixe "a=fmtp:<red_pt> ". cf. nego_fmtp §7 ph.2.
	static std::string GetT140RedFmtpParams(int t140PayloadType, int generations = 3);

	typedef std::map<int,Type> RTPMap;
};

// Catalogue des codecs texte réellement disponibles. Symétrique de
// AudioCodecFactory/VideoCodecFactory ; introduite pour la négociation fmtp.
// (Pas de Create* pour l'instant : la couche texte du MCU n'en a pas besoin.)
class TextCodecFactory
{
public:
	static const std::vector<TextCodec::Type>& GetSupportedCodecs();
};

class AppCodec
{
public:
	enum Type {BFCP=150};
	static const char* GetNameFor(Type type)
	{
		switch (type)
		{
			case BFCP:	return "BFCP";
			default:	return "unknown";
		}
	}

	// BFCP est un protocole applicatif (pas un codec ffmpeg) : toujours dispo.
	static bool IsSupported(Type codec);

	typedef std::map<int,Type> RTPMap;
};

static const char* GetNameForCodec(MediaFrame::Type media,DWORD codec)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return AudioCodec::GetNameFor((AudioCodec::Type)codec);
		case MediaFrame::Video:
			return VideoCodec::GetNameFor((VideoCodec::Type)codec);
		case MediaFrame::Text:
			return TextCodec::GetNameFor((TextCodec::Type)codec);
	}
	return "unknown media";
}
#endif
