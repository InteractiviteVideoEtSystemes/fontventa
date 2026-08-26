#ifndef _AUDIO_H_
#define _AUDIO_H_
#include <memory>
#include "config.h"
#include "media.h"
#include "codecs.h"

// AVFrame (échantillons décompressés refcomptés) — cf. SamplesPtr plus bas.
extern "C"
{
#include <libavutil/frame.h>
}

class AudioFrame;

// Les échantillons décompressés circulent dans des AVFrame refcomptés,
// enveloppés dans Samples — jumeau audio de Pict (medkit/video.h).
//
// La trame porte SES métadonnées (nb_samples, sample_rate, format, layout) :
// plus aucun tampon à dimensionner chez l'appelant, plus aucun contrat de
// fréquence à synchroniser entre producteur et consommateur.
//
// INVARIANT : le transport interne est du S16 mono. Les fabriques REFUSENT
// (nullptr) toute autre combinaison format/layout ; c'est au producteur de
// convertir (swresample) avant de publier.
class Samples;
typedef std::shared_ptr<Samples> SamplesPtr;

class Samples
{
public:
	Samples() : av_frame(nullptr) {}
	explicit Samples(AVFrame * frame) : av_frame(frame) {}
	virtual ~Samples() { if (av_frame) av_frame_free(&av_frame); }

	// Le partage se fait UNIQUEMENT via SamplesPtr : interdire la copie sinon
	// deux Samples libéreraient le même AVFrame (double-free).
	Samples(const Samples&)            = delete;
	Samples& operator=(const Samples&) = delete;

	AVFrame * GetAVFrame() const	{ return av_frame; }
	DWORD GetRate() const		{ return av_frame ? (DWORD)av_frame->sample_rate : 0; }
	DWORD GetNbSamples() const	{ return av_frame ? (DWORD)av_frame->nb_samples  : 0; }
	SWORD * GetData() const		{ return av_frame ? (SWORD*)av_frame->data[0]    : nullptr; }

	// AV_NOPTS_VALUE si la trame n'est pas horodatée. Base : 1/sample_rate.
	int64_t GetPTS() const		{ return av_frame ? av_frame->pts : AV_NOPTS_VALUE; }
	void SetPTS(int64_t pts)	{ if (av_frame) av_frame->pts = pts; }

	// ---- Fabriques ----------------------------------------------------------
	// Tampon neuf refcompté de nb échantillons S16 mono à `rate` Hz (non initialisé).
	static SamplesPtr Alloc(DWORD nb, DWORD rate);
	// Adoption d'un AVFrame déjà rempli : Samples en devient PROPRIÉTAIRE et le
	// libérera. Refuse (nullptr, sans libérer) une trame qui n'est pas S16 mono.
	static SamplesPtr FromAVFrame(AVFrame * frame);
	// Copie d'un tampon plat S16 mono (adaptateur des chaînes non encore migrées).
	static SamplesPtr FromBuffer(const SWORD * buffer, DWORD nb, DWORD rate);

	// Vrai si la trame respecte l'invariant de transport (S16, 1 canal).
	static bool IsS16Mono(const AVFrame * frame);

private:
	AVFrame * av_frame;
};

class AudioEncoder
{
public:
	virtual ~AudioEncoder() {}

	// Encode des échantillons de N'IMPORTE QUELLE taille : l'encodeur accumule
	// dans sa fifo interne et n'émet qu'une fois numFrameSamples atteint. Il
	// rééchantillonne lui-même si samples->GetRate() diffère de sa fréquence.
	//
	// Rend UNE trame encodée, propriété de l'encodeur, valide jusqu'au prochain
	// appel ; nullptr quand la fifo ne contient plus de trame complète. Un appel
	// peut donc en produire plusieurs : BOUCLER, samples=nullptr pour purger.
	//   for (AudioFrame* f = enc->EncodeFrame(s); f; f = enc->EncodeFrame(nullptr))
	virtual AudioFrame* EncodeFrame(SamplesPtr samples)=0;

	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	virtual DWORD GetClockRate()=0;
	virtual bool  GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp=""; return false; };

	// ADAPTATEUR TRANSITOIRE (non virtuel) vers l'ancienne interface plate, pour
	// les chaînes du mcu pas encore migrées. À SUPPRIMER avec le dernier appelant
	// (cf. design/audio-avframe.md §6). N'émet qu'une trame par appel et borne
	// l'écriture à outLen — l'appelant ne dimensionne plus rien à l'aveugle.
	int Encode(SWORD *in,int inLen,BYTE* out,int outLen);

	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;

protected:
	// Fréquence à laquelle l'appelant fournit ses échantillons (fréquence
	// pipeline), telle que passée à TrySetRate(). Peut différer de GetRate()
	// si le codec impose une autre fréquence. 0 = pas encore connue.
	DWORD			inputRate = 0;
};

class AudioDecoder
{
public:
	virtual ~AudioDecoder() {}

	// Consomme un paquet compressé. Les trames produites (0, 1 ou plusieurs)
	// sont ensuite retirées une à une par GetFrame(). Rend le nombre de trames
	// alors disponibles ; 0 aussi en cas d'erreur (journalisée).
	virtual int Decode(BYTE *in,int inLen)=0;

	// Retire la plus ancienne trame décodée (S16 mono à la fréquence native du
	// flux), nullptr quand il n'y en a plus. BOUCLER après chaque Decode().
	virtual SamplesPtr GetFrame()=0;

	virtual DWORD TrySetRate(DWORD rate)=0;
	virtual DWORD GetRate()=0;
	virtual bool  GetFmtpInfo(std::string &fmtp, int payloadType) { fmtp=""; return false; };

	// ADAPTATEUR TRANSITOIRE (non virtuel) — même statut que AudioEncoder::Encode.
	// Restitue au plus outLen échantillons par appel, le reste de la trame étant
	// retenu ici : l'appelant boucle avec (NULL,0) comme avant.
	int Decode(BYTE *in,int inLen,SWORD* out,int outLen);

	AudioCodec::Type	type;
	int			numFrameSamples;
	int			frameLength;

private:
	// État de l'adaptateur seul : trame en cours de recopie et offset atteint.
	SamplesPtr		pending;
	DWORD			pendingOffset = 0;
};

class AudioFrame : public MediaFrame
{
public:
	AudioFrame(AudioCodec::Type codec,DWORD rate, bool owns = true) : MediaFrame(MediaFrame::Audio,2048, owns)
	{
		//Store codec
		this->codec = codec;
		//Set default rate
		this->rate = rate;

		switch(codec)
		{
			// Codecs « sample-based » : chaque octet est un échantillon
			// indépendant, on découpe donc à 160 octets = 20 ms de RTP.
			case AudioCodec::PCMA:
			case AudioCodec::PCMU:
			case AudioCodec::G722:
				packetization = 160;
				break;
			// Codecs « frame-based » (Opus, AMR, GSM, Speex, AAC…) :
			// une trame codée est une unité indivisible qui doit tenir dans
			// UN paquet RTP. On met une valeur large (plafonnée au mtu par
			// Packetize) pour ne jamais fragmenter une trame en octets — sans
			// quoi le flux (Opus notamment) devient indécodable côté pair.
			default:
				packetization = 0xFFFF;
				break;
		}
	}

	virtual MediaFrame* Clone()
	{
		//Create new one
		AudioFrame *frame = new AudioFrame(codec,rate);
		//Copy content
		frame->SetMedia(buffer,length);
		//Duration
		frame->SetDuration(duration);
		//Set timestamp
		frame->SetTimestamp(GetTimeStamp());

		frame->packetization = this->packetization;
		//Return it
		return (MediaFrame*)frame;
	}

	AudioCodec::Type GetCodec() const			{ return codec;		}
	void	SetCodec(AudioCodec::Type codec)	{ this->codec = codec;	}
	DWORD	GetRate() const				{ return rate;		}

	virtual bool Packetize(unsigned int mtu);

private:
	AudioCodec::Type codec;
	DWORD		 rate;
	unsigned int packetization;
};

class AudioInput
{
public:
	virtual DWORD GetNativeRate()=0;
	virtual DWORD GetRecordingRate()=0;
	virtual int RecBuffer(SWORD *buffer,DWORD size)=0;
	virtual void  CancelRecBuffer()=0;
	virtual int StartRecording(DWORD samplerate)=0;
	virtual int StopRecording()=0;
};

class AudioOutput
{
public:
	virtual DWORD GetNativeRate()=0;
	virtual DWORD GetPlayingRate()=0;
	virtual int PlayBuffer(SWORD *buffer,DWORD size,DWORD frameTime)=0;
	virtual int StartPlaying(DWORD samplerate)=0;
	virtual int StopPlaying()=0;
};

class AudioCodecFactory
{
public:
	static AudioDecoder* CreateDecoder(AudioCodec::Type codec);
	// Variante avec extradata (AudioSpecificConfig/esds) : requise par les codecs
	// dont le décodeur a besoin de sa configuration (AAC des MP4). Ignoré par les
	// codecs qui n'en ont pas besoin.
	static AudioDecoder* CreateDecoder(AudioCodec::Type codec, const BYTE* extradata, DWORD extradataSize);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec);
	static AudioEncoder* CreateEncoder(AudioCodec::Type codec, const Properties &properties);

	// Liste des codecs audio réellement disponibles (candidats instanciables
	// filtrés par AudioCodec::IsSupported). Calculée une fois, mémoïsée,
	// thread-safe. Ordre = priorité de préférence.
	static const std::vector<AudioCodec::Type>& GetSupportedCodecs();
};

#endif
