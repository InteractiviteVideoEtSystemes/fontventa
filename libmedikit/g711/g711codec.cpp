/*
 * G.711 branché sur la base générique FfAudioEncoder/FfAudioDecoder (ffmpeg 5).
 * Remplace les tables de conversion maison (g711.c) et les quatre classes
 * écrites à la main : le codec suit désormais le même chemin que les autres.
 */
#include "g711codec.h"
#include <medkit/log.h>

PCMAEncoder::PCMAEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_PCM_ALAW, AudioCodec::PCMA)
{
	defaultSampleRate = 8000;
	TrySetRate(8000);
	Open();
}

PCMADecoder::PCMADecoder() :
	// Le PCM brut ne porte aucun en-tête : la fréquence doit être posée sur le
	// contexte AVANT l'ouverture, sinon le décodeur refuse de s'ouvrir.
	FfAudioDecoder(AV_CODEC_ID_PCM_ALAW, AudioCodec::PCMA, nullptr, 0, nullptr, 8000)
{
}

PCMUEncoder::PCMUEncoder(const Properties &properties) :
	FfAudioEncoder(properties, AV_CODEC_ID_PCM_MULAW, AudioCodec::PCMU)
{
	defaultSampleRate = 8000;
	TrySetRate(8000);
	Open();
}

PCMUDecoder::PCMUDecoder() :
	FfAudioDecoder(AV_CODEC_ID_PCM_MULAW, AudioCodec::PCMU, nullptr, 0, nullptr, 8000)
{
}
