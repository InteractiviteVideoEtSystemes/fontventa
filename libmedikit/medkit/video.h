#ifndef _VIDEO_H_
#define _VIDEO_H_
#include <memory>
#include "config.h"
#include "media.h"
#include "codecs.h"

// AVFrame (trame vidéo décompressée refcomptée) — cf. PicturePtr plus bas.
extern "C"
{
#include <libavutil/frame.h>
}


// The class videoframe rempresents an encoded (compressed) videoframe
// It is either ment to be fed into a VideoDecoder or sent over RTP.
class VideoFrame : public MediaFrame
{
public:
	VideoFrame(VideoCodec::Type codec,DWORD size, bool own = true) : MediaFrame(MediaFrame::Video,size, own)
	{
		//Store codec
		this->codec = codec;
		//Init values
		isIntra = 0;
		width = 0;
		height = 0;
		SetH264NalSizeLength(0);
	}

	virtual MediaFrame* Clone()
	{
		//Create new one
		VideoFrame *frame = new VideoFrame(codec,length);
		//Copy
		memcpy(frame->GetData(),buffer,length);
		//Set length
		frame->SetLength(length);
		//Size
		frame->SetWidth(width);
		frame->SetHeight(height);
		//Set intra
		frame->SetIntra(isIntra);
		//Set timestamp
		frame->SetTimestamp(GetTimeStamp());
		//Set duration
		frame->SetDuration(GetDuration());
		//Check if it has rtp info
		for (MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin();it!=rtpInfo.end();++it)
		{
			//Gete info
			MediaFrame::RtpPacketization *rtp = (*it);
			//Add it
			frame->AddRtpPacket(rtp->GetPos(),rtp->GetSize(),rtp->GetPrefixData(),rtp->GetPrefixLen(), rtp->IsMark());
		}
		//Return it
		return (MediaFrame*)frame;
	}

	VideoCodec::Type GetCodec()	{ return codec;			}
	bool  IsIntra()			{ return isIntra;		}
	DWORD GetWidth()		{ return width;			}
	DWORD GetHeight()		{ return height;		}

	void SetCodec(VideoCodec::Type codec)	{ this->codec = codec;		}
	void SetWidth(DWORD width)		{ this->width = width;		}
	void SetHeight(DWORD height)		{ this->height = height;	}
	void SetIntra(bool isIntra)		{ this->isIntra = isIntra;	}

	virtual bool Packetize(unsigned int mtu);

	/**
	 * Select how the NALU are to be parsed
	 * @param sz: how many bytes used to store NALU sizein the bitstream. 0 = use start code
	 **/
	void SetH264NalSizeLength(DWORD sz)
	{
		if (sz == 0)
		{
			useStartCode = true;
			naluSizeLen = 0;
		}
		else if (sz <= 4)
		{
			useStartCode = false;
			naluSizeLen = sz;
		}
		else
		{
			useStartCode = false;
			naluSizeLen = 4;
		}
	}

private:
	VideoCodec::Type codec;
	bool	isIntra;
	DWORD	width;
	DWORD	height;

	bool PacketizeH264(unsigned int mtu);
	bool PacketizeH263(unsigned int mtu);


	// H.264 specific
	bool useStartCode;
	DWORD naluSizeLen;

	// If NALU size is stored in data (MP4 file)
	DWORD ReadNaluSize(BYTE * data);

	//If butstream contains H.264 sync codes
	DWORD DetectNaluBoundary(BYTE * p, DWORD sz);

	// Handle fragmentation
	void PacketizeH264Nalu(unsigned int mtu, DWORD offset, DWORD naluSz, bool last);
};

// Uncompressed video pictures are stored in FFMPEG AVFrame structures wrapped in Pict.
class Pict;
typedef std::shared_ptr<Pict> PictPtr;

class Pict
{
public:
	Pict() : av_frame(nullptr) {}
	explicit Pict(AVFrame * frame) : av_frame(frame) {}
	virtual ~Pict() { if (av_frame) av_frame_free(&av_frame); }

	// Le partage se fait UNIQUEMENT via PictPtr (shared_ptr<Pict>) : interdire
	// la copie sinon deux Pict libéreraient le même AVFrame (double-free).
	Pict(const Pict&)            = delete;
	Pict& operator=(const Pict&) = delete;

	AVFrame * GetAVFrame() const { return av_frame; }
	DWORD GetWidth()  const { return av_frame ? av_frame->width  : 0; }
	DWORD GetHeight() const { return av_frame ? av_frame->height : 0; }

	// ---- Fabriques (producteurs de Pict immuables) ---------------------------
	// Charge un fichier image et le met à l'échelle en YUV420P. width/height=0 :
	// taille native ; l'un des deux à 0 : ratio conservé. Remplace Logo::Load.
	// nullptr en cas d'échec. Impl. dans logo.cpp.
	static PictPtr Load(const char* filename, int width = 0, int height = 0);
	// Trame noire YUV420P (Y=0, U=V=128). Remplace Logo::CreateBlack/Clean.
	static PictPtr CreateBlack(int width, int height);
	// Trame unie YUV420P remplie de (y,u,v). Sert de fond de mosaïque
	// (gris neutre Y=U=V=128, équivalent du memset -128 historique). nullptr en
	// cas de dimensions invalides. Impl. dans logo.cpp, à côté de CreateBlack.
	static PictPtr CreateColor(int width, int height, BYTE y, BYTE u, BYTE v);

	// True si la trame réside en mémoire GPU (surface matérielle VAAPI).
	bool IsGPUPict() const { return av_frame && av_frame->format == AV_PIX_FMT_VAAPI; }

	// Redescente EXPLICITE GPU->CPU. À n'appeler que sur un Pict GPU (cf. IsGPUPict()).
	// Renvoie un NOUVEAU Pict en YUV420P (CPU), ou nullptr en cas d'échec ou si la
	// trame est déjà en CPU. La politique de redescente appartient au CONSOMMATEUR :
	// le décodeur ne fait AUCUN download implicite, afin de préserver un pipeline
	// GPU de bout en bout (cf. avframe.md).
	PictPtr DownloadToCPU() const;

	// Envoi EXPLICITE CPU->GPU (upload vers une surface VAAPI). À n'appeler que sur
	// un Pict CPU. Place le Pict GPU résultant dans 'out' et renvoie 0 en cas de
	// succès ; renvoie un code AVERROR (<0) en cas d'échec, en particulier
	// AVERROR(ENOSYS) si l'accélération VAAPI n'est pas disponible sur la machine.
	int UploadToGPU(PictPtr& out) const;

	// Device VAAPI PARTAGÉ du processus (nullptr si l'accélération matérielle
	// est indisponible). Créé au plus une fois, jamais détruit. C'est LE device
	// commun : décodeurs, encodeurs, uploads et graphes de composition doivent
	// tous en dériver (av_buffer_ref), sinon les filtres *_vaapi refusent de
	// mélanger des trames issues de devices distincts. Sert aussi de sonde
	// « l'accélération matérielle est-elle disponible ? » (log de démarrage mcu).
	static AVBufferRef* GetVAAPIDevice();

private:
	AVFrame * av_frame;
};

class VideoInput
{
public:
        VideoInput() { sizeChanged = false; }

	virtual int   StartVideoCapture(int width,int height,int fps)=0;
	virtual PictPtr GrabFrame(DWORD timeout)=0;
	virtual void  CancelGrabFrame()=0;
	virtual DWORD GetBufferSize()=0;
	virtual int   StopVideoCapture() = 0;
        virtual DWORD GetNativeWidth() { return 0; }
        virtual DWORD GetNativeHeight() { return 0; }

        bool HasNativeSizeChanged()
        {
            if (sizeChanged)
            {
                sizeChanged = false;
                return true;
            }
            else
            {
                return false;
            }
        }

protected:
    bool sizeChanged;
};

class VideoOutput
{
protected:
	bool keepAspect;

public:
	void    KeepAspectRatio(bool keep) { keepAspect = keep; }
	bool	IsAspectRatioKept() { return keepAspect; }
	virtual int NextFrame(PictPtr pic)=0;
	// TODO : remove as the pict size can be obtain from the passed picture
	virtual int SetVideoSize(int width,int height)=0;
};



typedef std::shared_ptr<VideoFrame> VideoFramePtr;

class VideoEncoder
{
public:
	virtual ~VideoEncoder(){};

	virtual int SetSize(int width,int height)=0;
	virtual VideoFramePtr EncodeFrame(PictPtr pic)=0;
	virtual int FastPictureUpdate()=0;
	virtual int SetFrameRate(int fps,int kbits,int intraPeriod)=0;
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp =""; return false; };

public:
	VideoCodec::Type type;
};

class VideoDecoder
{
public:
	virtual ~VideoDecoder(){};

	virtual int GetWidth()=0;
	virtual int GetHeight()=0;
	virtual int Decode(BYTE *in,DWORD len) = 0;
	// Dépaquetisation RTP + décodage. Accumule le payload dépacketisé dans un
	// tampon interne et, sur 'last', décode la trame complète. ABI alignée sur
	// mcu/include/video.h (même position vtable, entre Decode et GetFrame).
	virtual int DecodePacket(BYTE *in,DWORD len,int lost,int last)=0;
	virtual PictPtr GetFrame()=0;
	virtual bool IsKeyFrame()=0;
	virtual bool GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp =""; return false; };
	// Acquittement de trame de référence (RPSI, RFC 7741 §5.1 — VP9 partage
	// le même contrat, RFC 9628 §5.1) : true si la DERNIÈRE trame décodée
	// avec succès met à jour une référence long-terme (VP8 : golden/altref,
	// remplacement ou copie) et portait un PictureID dans son payload
	// descriptor RTP. pictureId = la valeur telle que reçue (2 octets réseau
	// bit M inclus, ou 1 octet < 0x80) : l'émetteur la compare à l'identique.
	// En FIN de vtable : contrat d'ABI (cf. DecodePacket ci-dessus).
	virtual bool GetReferencePictureId(WORD &pictureId) { return false; }

public:
	VideoCodec::Type type;

};

class VideoCodecFactory
{
public:
	static VideoDecoder* CreateDecoder(VideoCodec::Type codec);
	static VideoEncoder* CreateEncoder(VideoCodec::Type codec);
	static VideoEncoder* CreateEncoder(VideoCodec::Type codec, const Properties &properties);

	// cf. AudioCodecFactory::GetSupportedCodecs.
	static const std::vector<VideoCodec::Type>& GetSupportedCodecs();

	// cf. AudioCodecFactory::GetSupportedEncoderCodecs.
	static const std::vector<VideoCodec::Type>& GetSupportedEncoderCodecs();
};
#endif
