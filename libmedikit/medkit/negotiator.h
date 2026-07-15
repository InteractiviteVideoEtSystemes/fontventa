#ifndef _NEGOTIATOR_H_
#define _NEGOTIATOR_H_

/**
 * negotiator.h — négociateur de codecs (phase 3 nego_fmtp).
 *
 * Composant libmedikit SANS dépendance MCU/JSR-309 (décision B), réutilisable
 * par le MCU et par la couche JSR-309. Il s'appuie sur le catalogue de
 * capacités (codecs.h : IsSupported / GetSupportedCodecs) et sur les
 * générateurs de fmtp « params seuls » de la phase 2
 * (*Encoder::GetFmtpParams / TextCodec::GetT140RedFmtpParams).
 *
 * Rôle (phase 3) :
 *   1. intersecter la RTPMap proposée par le contrôleur SIP (l'offer = menu,
 *      cf. §6.0) avec les codecs réellement supportés (décision D : un PT non
 *      supporté DISPARAÎT de la map acceptée) ;
 *   2. pour chaque PT retenu, dériver le fmtp local (params seuls, décision E)
 *      depuis les Properties locales, SANS ouvrir de codec (§3.2).
 *
 * Le fmtp DISTANT (remoteFmtp) est reçu dès maintenant dans la signature mais
 * IGNORÉ en phase 3 : son ingestion (contrainte de l'émission) est la phase 5.
 */

#include "config.h"
#include "media.h"
#include "codecs.h"
#include <map>
#include <vector>
#include <string>

// Un codec retenu par la négociation.
struct NegotiatedCodec
{
	int         payloadType;    // PT sur le fil (clé de la RTPMap)
	int         codec;          // AudioCodec/VideoCodec/TextCodec::Type (valeur)
	std::string fmtp;           // paramètres SEULS (vide si le codec n'a pas de fmtp)
	Properties  effectiveProps; // props résolues pour l'encodeur (= localProps
	                            // tant que remoteFmtp est ignoré, phase 3)
};

struct NegotiationResult
{
	// Sous-ensemble accepté (PT -> codec Type), à appliquer via SetReceivingRTPMap.
	std::map<int,int>            acceptedMap;
	// Détail par PT retenu, dans l'ordre des PT proposés.
	std::vector<NegotiatedCodec> codecs;
};

class CodecNegotiator
{
public:
	// media      : Audio / Video / Text (Application non négocié ici).
	// proposed   : RTPMap (PT -> codec Type) venant du contrôleur SIP.
	// localProps : props locales (config + défauts) pour dériver le fmtp local.
	// remoteFmtp : fmtp distant déjà ingéré — IGNORÉ en phase 3 (nullable).
	// out        : rempli avec la map filtrée + le détail par codec.
	// Retourne false si le média n'est pas négociable ; true sinon
	// (out.acceptedMap peut être vide si rien de proposé n'est supporté).
	static bool Negotiate(MediaFrame::Type media,
	                      const std::map<int,int>& proposed,
	                      const Properties& localProps,
	                      const Properties* remoteFmtp,
	                      NegotiationResult& out);
};

#endif
