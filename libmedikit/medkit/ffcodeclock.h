#ifndef _FFCODECLOCK_H_
#define _FFCODECLOCK_H_

#include <mutex>

/*
 * Sérialise l'OUVERTURE et la DESTRUCTION des contextes d'encodage ffmpeg.
 *
 * Pourquoi : libSvtAv1Enc 0.9.0 — la seule version packagée pour AlmaLinux 9
 * (EPEL, build de janvier 2022) — tient son inventaire de processeurs dans un
 * pointeur GLOBAL DE PROCESSUS, `lp_group` (Source/Lib/Encoder/Globals/
 * EbEncHandle.c), et le manipule sans le moindre verrou :
 *
 *   - svt_av1_enc_init_handle()   : `if (lp_group == NULL) EB_MALLOC(...)`
 *                                   — check-then-act sur un global ;
 *   - init_thread_management_params() : écrit `lp_group[socket_id]...`,
 *                                   appelé depuis le ctor de CHAQUE instance ;
 *   - svt_av1_enc_deinit_handle() : `EB_FREE(lp_group)` — libère le global
 *                                   INCONDITIONNELLEMENT, même quand d'autres
 *                                   instances d'encodeur sont vivantes.
 *
 * Deux pattes qui ouvrent ou ferment leur encodeur AV1 en même temps — ce que
 * fait tout ré-INVITE, qui redémarre les deux encodeurs à quelques dizaines de
 * millisecondes d'intervalle — font donc déréférencer NULL au premier arrivé :
 * SIGSEGV dans le thread d'encodage, mediaserver mort en pleine communication.
 * Constaté en trafic réel le 2026-08-13 (appel Linphone <-> Linphone en AV1
 * pur, crash 6 s après l'établissement de la vidéo, appel démoli en 488).
 *
 * `lp_group` n'est touché QUE pendant init_handle/deinit_handle, jamais pendant
 * l'encodage : sérialiser strictement ces deux fenêtres supprime la totalité de
 * la course, et ne coûte rien sur le chemin chaud (une ouverture d'encodeur par
 * établissement de patte, aucune prise de verrou par trame).
 *
 * Portée : les contextes d'ENCODAGE vidéo (FfVideoEncoder et ses dérivés, dont
 * AV1Encoder). Les décodeurs (libdav1d pour AV1) et les codecs audio n'ont pas
 * d'état global de ce type.
 *
 * À RETIRER avec le portage ffmpeg 9 + SVT-AV1 moderne : voir la fiche
 * ffmpeg9_migration_plan.md à la racine du dépôt mediaserver.
 */
inline std::mutex& FfCodecOpenLock()
{
	// C++11 et suivants : l'initialisation d'un statique local est
	// thread-safe, et `inline` garantit une instance unique pour toutes les
	// unités de compilation.
	static std::mutex lock;
	return lock;
}

#endif	/* _FFCODECLOCK_H_ */
