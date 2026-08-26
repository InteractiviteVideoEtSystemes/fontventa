#ifndef _FFAUDIOCODEC_H_
#define _FFAUDIOCODEC_H_
extern "C" {
#include <libavcodec/avcodec.h>	// AVCodec, AVCodecContext, AVFrame, AVPacket, enum AVCodecID
}

#include "medkit/codecs.h"
#include "medkit/audio.h"
#include <list>

// Nécessaires seulement dans le .cpp : pointeurs opaques ici.
struct SwrContext;
struct AVAudioFifo;

bool MapAudioCodec( enum AVCodecID id, AudioCodec::Type & out );

/**
 * Encodeur audio générique adossé à ffmpeg (libavcodec + libswresample).
 *
 * Cycle de vie attendu :
 *   1. construction (recherche du codec, allocation du contexte, mono) ;
 *   2. la classe dérivée règle bitrate / options propres au codec ;
 *   3. TrySetRate() fixe le format d'échantillon et la fréquence, et crée
 *      au besoin le rééchantillonneur S16 -> format natif du codec ;
 *   4. Open() ouvre effectivement le codec (frame_size connu après coup).
 *
 * L'encodeur possède SA fifo (av_audio_fifo) : il accepte des SamplesPtr de
 * n'importe quelle taille et n'émet qu'une fois numFrameSamples réunis. C'est
 * le SEUL endroit du pipeline qui redécoupe en trames fixes.
 */
class FfAudioEncoder : public AudioEncoder
{
public:
	// codec_name : si non nul, tente avcodec_find_encoder_by_name(codec_name) en
	// premier (pour choisir le codec natif plutôt qu'un wrapper), avec repli sur
	// avcodec_find_encoder(av_codec). Laisser à nullptr pour le comportement par défaut.
	FfAudioEncoder(const Properties& properties, enum AVCodecID av_codec, AudioCodec::Type codec_id,
	               const char* codec_name = nullptr);
	virtual ~FfAudioEncoder();

	// L'adaptateur plat de la classe de base reste joignable sur un
	// FfAudioEncoder concret (sinon masqué par la surcharge ci-dessous).
	using AudioEncoder::Encode;

	virtual AudioFrame* EncodeFrame(SamplesPtr samples);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate();
	virtual DWORD GetClockRate()	{ return GetRate(); }

protected:
	// Ouvre le codec une fois la configuration faite par la classe dérivée.
	bool Open();

	bool IsSigned16FmtSupported() const;
	bool IsRateNativelySupported(DWORD rate) const;

	// (Ré)alloue le tampon de la trame d'entrée pour `nb` échantillons.
	bool EnsureFrame(int nb);

	// (Re)configure le rééchantillonneur pour une entrée à `rate` Hz. Appelé par
	// TrySetRate et, à chaud, dès qu'une trame arrive à une autre fréquence.
	bool SetupResampler(DWORD rate);

	// Vide dans la fifo ce que le rééchantillonneur retient encore. À appeler
	// avant de le reconfigurer, sinon ces échantillons sont perdus.
	void DrainResampler();

	// Verse `samples` dans la fifo (via le resampler si nécessaire).
	bool PushToFifo(SamplesPtr samples);

	// Retire numFrameSamples de la fifo et les encode dans `out`. Faux si la
	// fifo est trop courte ou si l'encodage n'a rien produit.
	bool EncodeFromFifo();

	// Fréquence de repli si la fréquence demandée n'est pas supportée.
	// À régler par la classe dérivée avant TrySetRate().
	DWORD defaultSampleRate;

	const AVCodec	*codec;
	AVCodecContext	*ctx;
	SwrContext	*swr;	// nullptr si pas de rééchantillonnage nécessaire
	AVAudioFifo	*fifo;	// échantillons au format/fréquence du codec
	AVFrame		*frame;	// trame d'entrée réutilisée (une trame codec)
	AVPacket	*pkt;	// paquet de sortie réutilisé
	AudioFrame	*out;	// trame encodée rendue par EncodeFrame, réutilisée
	bool		 opened;
	int		 allocatedSamples;	// capacité du tampon de `frame`
	int64_t		 nextPts;		// horodatage de la prochaine trame émise
};

/**
 * Décodeur audio générique adossé à ffmpeg.
 *
 * Décode les paquets compressés et restitue du PCM signé 16 bits mono à la
 * fréquence native du flux. Si le décodeur produit un autre format
 * (planar/float, stéréo...), un rééchantillonneur libswresample convertit vers
 * S16 mono. Chaque trame décodée est publiée TELLE QUELLE, dans son propre
 * AVFrame refcompté : plus aucun redécoupage, plus aucun tampon plafonné.
 */
class FfAudioDecoder : public AudioDecoder
{
public:
	// codec_name : si non nul, tente avcodec_find_decoder_by_name(codec_name) en
	// premier (pour choisir le décodeur natif ou un wrapper particulier plutôt que
	// celui qu'avcodec_find_decoder() retournerait par défaut). Laisser à nullptr
	// pour le comportement par défaut.
	FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id,
	               const char* codec_name = nullptr);
	// Variante avec extradata (AudioSpecificConfig / avcC audio) : indispensable
	// pour l'AAC raw des MP4 (sans en-tête ADTS), qui ne se décode pas sans sa
	// config. extradata peut être NULL (équivaut au ctor à 2 arguments).
	// sample_rate : fréquence posée sur le contexte AVANT avcodec_open2, pour
	// les décodeurs qui l'exigent à l'ouverture quand il n'y a pas d'extradata
	// (le speex natif de ffmpeg refuse d'ouvrir sans elle — constaté en trafic
	// le 2026-08-14 : « could not open decoder » sur chaque trame). 0 = non posée.
	FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id,
	               const uint8_t* extradata, int extradata_size,
	               const char* codec_name = nullptr, int sample_rate = 0);
	virtual ~FfAudioDecoder();

	// Idem FfAudioEncoder : garder l'adaptateur plat visible.
	using AudioDecoder::Decode;

	virtual int        Decode(BYTE *in, int inLen);
	virtual SamplesPtr GetFrame();
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate();

	// Primitive de disponibilité pour les codecs adossés à ffmpeg : vrai si
	// libavcodec fournit un décodeur (par nom préféré si fourni, sinon par ID) —
	// même test/repli que le constructeur. Utilisée par le IsSupported() des
	// codecs concrets ffmpeg ; les codecs sur une AUTRE lib ne l'appellent pas.
	static bool IsCodecAvailable(enum AVCodecID id, const char* preferredName = nullptr);

private:
	// Profondeur maximale de la file de sortie (1 s à 50 trames/s).
	static const size_t MaxPendingFrames = 50;

	// Publie `src` (déjà S16 mono) dans la file de sortie, en lui volant son
	// tampon (av_frame_move_ref) : aucune recopie d'échantillons.
	bool PublishS16Mono(AVFrame *src);
	// Convertit `src` vers S16 mono puis le publie.
	bool ConvertAndPublish(AVFrame *src);

	const AVCodec	*codec;
	AVCodecContext	*ctx;
	SwrContext	*swr;	// nullptr tant que la sortie est déjà S16 mono
	AVFrame		*frame;	// trame décodée réutilisée
	AVPacket	*pkt;	// paquet d'entrée réutilisé
	bool		 opened;
	std::list<SamplesPtr> frames;	// trames prêtes, dans l'ordre
};

#endif
