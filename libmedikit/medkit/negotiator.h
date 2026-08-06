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

// Découpe une chaîne de paramètres fmtp SEULS (ce qui suit "a=fmtp:<pt> ", ex.
// "profile-level-id=42e01f;packetization-mode=1") en couples clé→valeur.
//
// Les noms de paramètres SDP étant insensibles à la casse, les clés sont
// normalisées en minuscules ; les espaces autour de la clé, du '=' et de la valeur
// sont retirés. Un élément sans '=' est conservé avec une valeur vide (certains
// paramètres sont des drapeaux nus). Une chaîne vide donne une map vide.
std::map<std::string,std::string> ParseFmtpParams(const std::string& params);

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
	// remoteFmtp : fmtp du pair, portant les paramètres SEULS, sous l'une des deux
	//              clés de la convention §5.3 de nego_fmtp.md :
	//                "pt.<pt>.fmtp"    par PAYLOAD TYPE — la seule correcte dès qu'un
	//                                  codec est offert sous plusieurs PT (une offre
	//                                  navigateur énumère H.264 sous six ou sept PT
	//                                  pour décrire autant de couples profil/mode).
	//                                  Alimentée par le paramètre `offer` de
	//                                  StartReceiving (MCU).
	//                "<nomcodec>.fmtp" par nom de codec (ex. "h264.fmtp"), la
	//                                  convention historique, alimentée par
	//                                  `codec.<x>.fmtp` via SetRTPProperties
	//                                  (JSR-309, un PT par codec).
	//              La clé par PT gagne quand les deux sont présentes. NULL ⇒ pas
	//              d'entrée distante, on annonce notre config telle quelle.
	//
	//              La résolution est donc **par payload type** : deux PT du même codec
	//              peuvent repartir avec deux fmtp différents, ce que RFC 6184 §8.2.2
	//              exige et ce que la clé par codec, seule, rendait impossible.
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
