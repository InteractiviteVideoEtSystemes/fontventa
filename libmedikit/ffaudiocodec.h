#ifndef _FFAUDIOCODEC_H_
#define _FFAUDIOCODEC_H_
extern "C" {
#include <libavcodec/avcodec.h>	// AVCodec, AVCodecContext, AVFrame, AVPacket, enum AVCodecID
}

#include "medkit/codecs.h"
#include "medkit/audio.h"
#include "medkit/fifo.h"
#include <list>

// libswresample n'est nécessaire que dans le .cpp : pointeur opaque ici.
struct SwrContext;

/**
 * Encodeur audio générique adossé à ffmpeg (libavcodec + libswresample).
 *
 * Cycle de vie attendu :
 *   1. construction (recherche du codec, allocation du contexte, mono) ;
 *   2. la classe dérivée règle bitrate / options propres au codec ;
 *   3. TrySetRate() fixe le format d'échantillon et la fréquence, et crée
 *      au besoin le rééchantillonneur S16 -> format natif du codec ;
 *   4. Open() ouvre effectivement le codec (frame_size connu après coup).
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

	virtual int   Encode(SWORD *in, int inLen, BYTE* out, int outLen);
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

	// Fréquence de repli si la fréquence demandée n'est pas supportée.
	// À régler par la classe dérivée avant TrySetRate().
	DWORD defaultSampleRate;

	// Taux d'entrée tel que passé à TrySetRate() (taux pipeline du MCU).
	// Peut différer de ctx->sample_rate si une conversion de fréquence est active.
	DWORD inputRate;

	const AVCodec	*codec;
	AVCodecContext	*ctx;
	SwrContext	*swr;	// nullptr si pas de rééchantillonnage nécessaire
	AVFrame		*frame;	// trame d'entrée réutilisée
	AVPacket	*pkt;	// paquet de sortie réutilisé
	bool		 opened;
	int		 allocatedSamples;	// capacité du tampon de `frame`
};

/**
 * Décodeur audio générique adossé à ffmpeg.
 *
 * Décode les paquets compressés et restitue du PCM signé 16 bits mono à la
 * fréquence native du flux. Si le décodeur produit un autre format
 * (planar/float, stéréo...), un rééchantillonneur libswresample convertit vers
 * S16 mono. Les échantillons décodés sont accumulés dans une fifo et restitués
 * par tranches de numFrameSamples (comportement aligné sur les autres codecs).
 */
class FfAudioDecoder : public AudioDecoder
{
public:
	FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id);
	// Variante avec extradata (AudioSpecificConfig / avcC audio) : indispensable
	// pour l'AAC raw des MP4 (sans en-tête ADTS), qui ne se décode pas sans sa
	// config. extradata peut être NULL (équivaut au ctor à 2 arguments).
	FfAudioDecoder(enum AVCodecID av_codec, AudioCodec::Type codec_id,
	               const uint8_t* extradata, int extradata_size);
	virtual ~FfAudioDecoder();
	virtual int   Decode(BYTE *in, int inLen, SWORD* out, int outLen);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate();

private:
	const AVCodec	*codec;
	AVCodecContext	*ctx;
	SwrContext	*swr;	// nullptr tant que la sortie est déjà S16 mono
	AVFrame		*frame;	// trame décodée réutilisée
	AVPacket	*pkt;	// paquet d'entrée réutilisé
	bool		 opened;
	fifo<SWORD,8192>  samples;
};

#endif
