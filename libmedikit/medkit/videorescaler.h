#ifndef _VIDEORESCALER_H_
#define _VIDEORESCALER_H_

#include "medkit/video.h"
extern "C"
{
#include <libavfilter/avfilter.h>
}

// Redimensionneur vidéo à graphe avfilter PERSISTANT (reconfiguré uniquement au
// changement de paramètres), unifiant CPU (filtre 'scale') et GPU (filtre
// 'scale_vaapi'). Remplace FrameScaler ; détenu comme membre par les pipes qui
// redimensionnent (cf. avframe.md). Non copiable (possède un graphe ffmpeg).
class VideoRescaler
{
public:
	VideoRescaler();
	~VideoRescaler();

	VideoRescaler(const VideoRescaler&)            = delete;
	VideoRescaler& operator=(const VideoRescaler&) = delete;

	// Redimensionne 'in' et renvoie un NOUVEAU Pict.
	//  - keepRatio=false : sortie exactement width x height.
	//  - keepRatio=true  : conserve 'width' et recalcule la hauteur effective
	//    d'après le ratio source ('height' est ignoré).
	// Si la trame est déjà à la taille cible, renvoie un partage (référence)
	// zéro-copie. nullptr en cas d'échec.
	PictPtr Rescale(const PictPtr& in, int width, int height, bool keepRatio);

private:
	// (Re)construit le graphe si un paramètre a changé.
	bool Configure(int inW, int inH, int inFmt, int outW, int outH, AVBufferRef* hwFramesCtx);
	void Release();

	AVFilterGraph*   graph;
	AVFilterContext* srcCtx;
	AVFilterContext* sinkCtx;

	// Paramètres du graphe courant (reconfiguration paresseuse).
	int curInW, curInH, curInFmt;
	int curOutW, curOutH;
	AVBufferRef* curHwFramesCtx;
};

#endif
