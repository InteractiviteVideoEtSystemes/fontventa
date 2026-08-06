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
	//  - keepRatio=false : sortie exactement width x height, ÉTIRÉE si le ratio
	//    source diffère.
	//  - keepRatio=true  : conserve 'width' et recalcule la hauteur effective
	//    d'après le ratio source ('height' est ignoré) — la sortie n'a donc PAS la
	//    taille demandée, ce qui ne convient pas à un encodeur.
	// Si la trame est déjà à la taille cible, renvoie un partage (référence)
	// zéro-copie. nullptr en cas d'échec.
	PictPtr Rescale(const PictPtr& in, int width, int height, bool keepRatio);

	// Letterbox : sortie EXACTEMENT width x height, ratio source conservé, bandes
	// noires centrées sur le côté qui manque.
	//
	// C'est ce qu'il faut quand la taille de sortie est imposée (un encodeur) et que le
	// ratio d'entrée n'est pas le même : `Rescale(..., false)` étire — une toile de
	// mosaïque 4:3 encodée en 16:9 élargit chaque vignette de 33 %, ce qui s'est vu sur
	// un appel du 2026-08-06 — et `Rescale(..., true)` rend une autre taille que celle
	// demandée, donc ne convient pas non plus. Aucune des deux ne remplaçait celle-ci.
	PictPtr Letterbox(const PictPtr& in, int width, int height);

private:
	// (Re)construit le graphe si un paramètre a changé. `letterbox` ajoute la
	// conservation du ratio et le remplissage noir à la taille demandée.
	bool Configure(int inW, int inH, int inFmt, int outW, int outH, AVBufferRef* hwFramesCtx,
	               bool letterbox = false);
	void Release();

	AVFilterGraph*   graph;
	AVFilterContext* srcCtx;
	AVFilterContext* sinkCtx;

	// Corps commun de Rescale/Letterbox : la seule différence est le graphe construit.
	PictPtr Run(const PictPtr& in, int outW, int outH, bool letterbox);

	// Paramètres du graphe courant (reconfiguration paresseuse).
	int curInW, curInH, curInFmt;
	int curOutW, curOutH;
	bool curLetterbox = false;   // clé de reconfiguration : scale seul ou scale+pad
	AVBufferRef* curHwFramesCtx;
};

#endif
