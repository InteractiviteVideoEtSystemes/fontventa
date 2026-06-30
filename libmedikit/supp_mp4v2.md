# Suppression de mp4v2 — passage à ffmpeg / libavformat

> Plan de travail pour remplacer la dépendance **mp4v2** de `libmedikit`
> (`mp4recorder`, `mp4player`, `mp4track`) par **libavformat** (ffmpeg 5.1,
> libavformat 59 déjà disponible dans `/usr/include/ffmpeg`).
>
> Statut : **brouillon à relire manuellement** (E. Buu). Rien n'est implémenté.
>
> **Décisions de conception déjà actées** (cf. §3 et §4.3) :
> 1. **Pas de façade** : on passe l'`AVFormatContext*` directement dans l'API ;
>    `app_mp4` ouvre le fichier via libavformat et transmet le handle au
>    recorder / player. On **modifie le contrat**.
> 2. **Packetisation RTP différée à l'émission, par FFmpeg (option D)** : la
>    `VideoFrame` retournée par `GetNextFrame` ne contient **que le flux
>    élémentaire décodable** (aucune `RtpPacketizationInfo`). Le découpage RTP
>    est fait **plus tard, au moment d'émettre**, par un packetiseur FFmpeg
>    réutilisable invoqué dans `Mp4PlayerPlayNextFrame()` (et, à terme, dans le
>    mediaserver). Plus de pistes hint, plus de `Packetize()` maison.

---

## 1. Contexte et objectif

`libmedikit` lit et écrit des fichiers MP4 via la bibliothèque historique
**mp4v2** (`<mp4v2/mp4v2.h>`, bâtie en statique par `install.ksh` dans
`staticdeps/`). Trois unités de compilation en dépendent :

| Fichier | Rôle |
|---|---|
| `mp4track.{h,cpp}` | Abstraction d'une piste (audio / vidéo / sous-titre) : création, écriture, lecture échantillon par échantillon. C'est **tout le contact réel avec mp4v2**. |
| `mp4recorder.{h,cpp}` | Orchestration de l'enregistrement multi-pistes à partir de `MediaFrame` medkit. Ne touche mp4v2 que pour les *tags* (`MP4Tags*`). |
| `mp4player.{h,cpp}` | Orchestration de la lecture : sélection des pistes, ordonnancement temporel des trames. Touche mp4v2 pour l'énumération des pistes (`MP4FindTrackId`, `MP4GetTrackType`, hint payload). |

L'objectif est de **supprimer mp4v2** au profit de libavformat, déjà tirée par
le projet (chantier ffmpeg 5 en cours, cf. `FfAudioEncoder`/`FfVideoEncoder`),
afin de :

- n'avoir qu'une seule dépendance multimédia (ffmpeg) à maintenir et à porter
  sur AlmaLinux 9 ;
- supprimer une dépendance statique vieillissante et non packagée.

### Périmètre

- **Inclus** : `mp4track`, `mp4recorder`, `mp4player` de
  `third_party/fontventa/libmedikit` + leur liant Asterisk `mp4format.cpp`.
- **À surveiller mais hors périmètre immédiat** : `mcu/src/mp4recorder.cpp`,
  `mcu/src/mp4player.cpp`, `mcu/src/mp4streamer.cpp` sont une **autre**
  implémentation mp4v2 (côté serveur MCU). Elle n'est pas traitée ici mais
  devra suivre la même approche ultérieurement. Le **packetiseur RTP FFmpeg**
  conçu au §4.3 est volontairement un composant medkit autonome, pour être
  **réutilisé** par cette logique côté mediaserver.
- Les outils `tools/mp4*.cpp`, `mp4creator/`, `app_mp4/app_mp4.c` ouvrent
  eux-mêmes le fichier (voir §3). On doit supprimer la création des pistes de hint.
- Le script IVES_convert.ksh qui ne doit plus hinter les vidéos converties.


---

## 2. Inventaire de l'API mp4v2 utilisée

C'est la surface exacte à reproduire. Tout est dans `mp4track.cpp` sauf mention.

### 2.1 Ouverture / fermeture du fichier (chez l'appelant, pas dans libmedikit)

`MP4Read`, `MP4Create`, `MP4CreateEx`, `MP4Modify`, `MP4Close` — appelés dans
`app_mp4/app_mp4.c`, `tools/*.cpp`. Le `MP4FileHandle` **traverse l'API
publique** (`mp4recorder.h`, `mp4player.h`, `mp4format.h`). → nouveau contrat §3.

### 2.2 Écriture (enregistrement)

| Fonction mp4v2 | Usage |
|---|---|
| `MP4AddALawAudioTrack` / `MP4AddULawAudioTrack` | pistes G711 a/u-law |
| `MP4AddAudioTrack` (`MP4_PCM16_BIG_ENDIAN_AUDIO_TYPE`, `MP4_MPEG2_AAC_LC_AUDIO_TYPE`) | SLIN, AAC |
| `MP4AddAmrAudioTrack` | AMR |
| `MP4AddH263VideoTrack` / `MP4AddH264VideoTrack` | vidéo |
| `MP4AddSubtitleTrack` | sous-titres (tx3g) |
| `MP4SetTrackESConfiguration` | config AAC (`AudioSpecificConfig`) |
| `MP4SetAudioProfileLevel` | profil AMR |
| `MP4SetTrackIntegerProperty` | `…alaw/ulaw/slin.channels`, `.sampleSize`, `avc1.width/height` |
| `MP4AddH264SequenceParameterSet` / `MP4AddH264PictureParameterSet` | SPS/PPS dans `avcC` |
| `MP4SetTrackName` | nom de piste (nom du participant) |
| `MP4WriteSample` | écriture d'un échantillon (durée, sync flag) |
| `MP4TagsAlloc` / `…SetEncodingTool` / `…SetArtist` / `…SetComments` / `…Store` / `…Free` | métadonnées (commentaire = texte des sous-titres) — dans `mp4recorder.cpp` |

### 2.3 Pistes de *hint* RTP (écriture **et** lecture) — **supprimées**

| Fonction | Usage (devenu inutile, cf. §4.3) |
|---|---|
| `MP4AddHintTrack`, `MP4SetHintTrackRtpPayload` | crée une piste hint RTP et y fixe le type de charge utile |
| `MP4AddRtpHint`, `MP4AddRtpPacket`, `MP4AddRtpSampleData`, `MP4AddRtpImmediateData`, `MP4WriteRtpHint` | écrit, paquet par paquet, la packetisation RTP exacte |
| `MP4ReadRtpHint`, `MP4ReadRtpPacket` | relit cette packetisation à la lecture |
| `MP4GetHintTrackReferenceTrackId`, `MP4GetHintTrackRtpPayload` | retrouve la piste média associée à un hint + son codec |

> Ces fonctions n'ont **pas d'équivalent public** dans libavformat (lecture
> paquet par paquet d'une piste hint). La décision §4.3 (packetisation différée
> à l'émission, par FFmpeg) supprime totalement le besoin de pistes hint.

### 2.4 Lecture

| Fonction | Usage |
|---|---|
| `MP4FindTrackId(type, idx)` | énumère pistes par type (`MP4_HINT_TRACK_TYPE`, `MP4_AUDIO_TRACK_TYPE`, `MP4_VIDEO_TRACK_TYPE`, `MP4_SUBTITLE_TRACK_TYPE`) |
| `MP4GetTrackType`, `MP4GetTrackMediaDataName` | type/codec d'une piste |
| `MP4GetTrackTimeScale` | échelle de temps → conversion ms |
| `MP4GetSampleTime`, `MP4GetSampleDuration`, `MP4GetSampleSize` | timing/taille d'un échantillon |
| `MP4ConvertFromTrackTimestamp(…, 1000)` | ts piste → millisecondes |
| `MP4ReadSample` | lit un échantillon (données + ts + durée + sync) |
| `MP4GetTrackH264SeqPictHeaders`, `MP4GetTrackH264LengthSize` | SPS/PPS + taille du préfixe de longueur NALU |

### 2.5 Types mp4v2 dans les en-têtes publics

`MP4FileHandle`, `MP4TrackId`, `MP4Timestamp`, `MP4Duration`, `MP4SampleId`,
`MP4_INVALID_TRACK_ID`, `MP4_INVALID_TIMESTAMP`. Présents dans `mp4track.h`,
`mp4recorder.h`, `mp4player.h`, `mp4format.h` → remplacés (§3).

---

## 3. Nouveau contrat : `AVFormatContext*` traversant l'API (décision actée)

**Décision : pas de façade opaque.** `app_mp4` (et les outils) ouvrent le
fichier directement avec libavformat et transmettent l'`AVFormatContext*` au
recorder / player. On **modifie le contrat** des en-têtes publics.

### 3.1 Signatures

```c
// astmedkit/mp4format.h — vu depuis app_mp4.c (C pur ; avformat.h est C-safe)
struct mp4rec  * Mp4RecorderCreate(struct ast_channel*, AVFormatContext* fmt, ...);
struct mp4play * Mp4PlayerCreate  (struct ast_channel*, AVFormatContext* fmt, ...);
```

```cpp
// mp4recorder.h / mp4player.h / mp4track.h
//   - retirer  #include <mp4v2/mp4v2.h>
//   - ajouter  extern "C" { #include <libavformat/avformat.h> }
//   - MP4FileHandle mp4  →  AVFormatContext * fmt
//   - MP4TrackId         →  int streamIndex (indice dans fmt->streams[])
//   - MP4_INVALID_TRACK_ID → -1
```

### 3.2 Répartition du cycle de vie (le cœur du contrat)

libavformat impose en écriture l'ordre strict
`alloc → new_stream×N → write_header → write_frame×N → write_trailer`. On le
**scinde** entre l'appelant (ouverture/fermeture du fichier) et le recorder
(logique de muxing) :

**Enregistrement (muxer mp4) :**

| Étape | Responsable |
|---|---|
| `avformat_alloc_output_context2(&fmt, NULL, "mp4", path)` + `avio_open(&fmt->pb, …, WRITE)` | **app_mp4** |
| `avformat_new_stream` + remplissage `codecpar` (1 par piste) | **recorder** (`Mp4*Track::Create`) |
| `avformat_write_header` — **différé à la 1ʳᵉ trame traitée** (une seule fois) | **recorder** (`ProcessFrame`) |
| `av_interleaved_write_frame` | **recorder** |
| `av_dict_set(fmt->metadata, "comment", texteST)` puis `av_write_trailer` | **recorder** (destructeur / `Flush`) |
| `avio_closep(&fmt->pb)` + `avformat_free_context` | **app_mp4**, *après* `Mp4RecorderDestroy` |

**Lecture (demuxer) — pas de contrainte d'ordre :**

| Étape | Responsable |
|---|---|
| `avformat_open_input` + `avformat_find_stream_info` | **app_mp4** |
| parcours `fmt->streams[]`, `av_read_frame` | **player** |
| `avformat_close_input` | **app_mp4**, *après* `Mp4PlayerDestroy` |

### 3.3 Conséquence forcée : plus d'auto-création paresseuse de piste

L'en-tête étant figé dès la 1ʳᵉ trame, **toutes les pistes doivent être
déclarées avant**. Or aujourd'hui la piste **audio est auto-créée
paresseusement** à la première trame audio (`mp4recorder::ProcessFrame` →
`AddTrack(f2->GetCodec()…)`), de même que le texte ; seules vidéo et texte sont
déclarées dans `Mp4RecorderCreate` (`mp4format.cpp:302/306`).

`Mp4RecorderCreate` connaît déjà le format audio du canal
(`mp4format.cpp:289-296`, il force même ULAW). Donc :

- **déclarer la piste audio dès `Mp4RecorderCreate`** (comme vidéo/texte) ;
- **supprimer** les blocs d'auto-création dans `mp4recorder::ProcessFrame`
  (audio + texte).

> C'est le seul vrai **changement de comportement** imposé par libavformat.
> Point de revue.

### 3.4 Métadonnées (commentaire = texte des sous-titres) — résolu

Le texte des sous-titres pour la VM n'est connu qu'en fin d'enregistrement.
Le muxer mov/mp4 écrit le `moov` (donc les métadonnées) au `av_write_trailer`.
Il suffit donc d'appeler `av_dict_set(fmt->metadata, …)` **juste avant**
`av_write_trailer` dans le destructeur du recorder. L'inquiétude initiale
(métadonnées posées trop tard) disparaît.

### 3.5 Gestion d'erreur d'ouverture

Aujourd'hui l'appelant teste `mp4 == MP4_INVALID_*`. Avec libavformat il faut
gérer les **codes de retour négatifs** : `avformat_open_input(...) < 0`,
`avio_open(...) < 0`, `avformat_alloc_output_context2(...) < 0`.

---

## 4. Conception de la nouvelle implémentation

### 4.1 Principe général

Conserver **strictement** la logique d'orchestration de `mp4recorder` et
`mp4player` (délais initiaux, prologue vidéo, ordonnancement, RED/RTT, EOF).
Seules changent (a) la couche `mp4track` (réécrite sur libavformat) et (b) le
type du handle (`MP4FileHandle` → `AVFormatContext*`, §3).

```
                    quasi inchangé                 réécrit sur libavformat
   ast_frame ─▶ Ast{Recorder,Player} ─▶ {mp4recorder,mp4player} ─▶ Mp4*Track ─▶ AVFormatContext
   MediaFrame ─────────────────────────────┘
```

### 4.2 Accès au fichier via `AVFormatContext`

- **Écriture** : `avformat_new_stream` par piste (`Create`),
  `av_interleaved_write_frame` (`ProcessFrame`), `write_header` différé,
  `write_trailer` en fin (cf. §3.2).
- **Lecture** : parcours de `fmt->streams[]` (remplace `MP4FindTrackId`),
  `av_read_frame` (remplace `MP4ReadSample`).
- **Timing** : `av_rescale_q` entre `stream->time_base` et la base
  millisecondes (audio) / 90 kHz (vidéo). Remplace
  `MP4ConvertFromTrackTimestamp` / `MP4GetTrackTimeScale`.
- **Métadonnées** : `av_dict_set` (cf. §3.4). Remplace `MP4Tags*`.

### 4.3 Packetisation RTP différée à l'émission, par FFmpeg (option D retenue)

**Suppression totale des pistes hint.** La question est : où et comment
produire le découpage RTP une fois les hint supprimés ? Plusieurs options ont
été étudiées ; la contrainte décisive est l'**invariant MCU** ci-dessous.

#### Invariant requis (usage MCU)

> Une `VideoFrame` issue de la lecture peut être **décodée** (transcodage /
> mixage MCU). Son buffer doit donc contenir le **flux élémentaire décodable**
> — et **pas** des payloads RTP fragmentés (FU-A, STAP-A).

#### Rappel du modèle de packetisation medkit

Une `MediaFrame` porte un buffer + une liste `RtpPacketizationInfo`. Chaque
entrée `RtpPacketization` (`media.h:42-46`) décrit un paquet RTP : `pos`/`size`
→ tranche contiguë `buffer[pos..pos+size]`, `prefix[16]`/`prefixLen` → octets
préfixés, `mark` → marqueur. Soit **payload = `prefix` ++ tranche**.

#### Options étudiées

| Opt. | Idée | Verdict |
|---|---|---|
| **A** | Garder le buffer élémentaire ; packetiser avec le `Packetize()` **maison** medkit (par codec) | ❌ il faut **réécrire un packetiseur à chaque nouveau codec vidéo** (objectif non atteint) ; et `PacketizeH263` n'existe même pas |
| **B** | La `VideoFrame` stocke les **payloads RTP** produits par FFmpeg | ❌ **non décodable** → casse transcodage / MCU (viole l'invariant) |
| **C** | Buffer élémentaire + payloads FFmpeg dans une **liste parallèle** (étendre `RtpPacketization` pour qu'un paquet possède ses octets) | ❌ change `media.h` **et** les consommateurs (émetteur RTP, `MediaFrameToAstFrame2`) ; blast radius hors libmedikit |
| **D** | **Ne pas packetiser dans la `VideoFrame`.** Elle ne porte que le flux élémentaire. Le découpage RTP est fait **à l'émission**, par un packetiseur FFmpeg, dans `Mp4PlayerPlayNextFrame()` | ✅ **retenue** |

#### Conception (option D)

1. `mp4player::GetNextFrame` retourne une `VideoFrame` **élémentaire**, **sans**
   `RtpPacketizationInfo` (on **n'appelle plus** `Packetize`). Le buffer reste
   décodable → transcodage / MCU OK.
2. La packetisation est faite **au moment d'émettre**, dans
   `Mp4PlayerPlayNextFrame()` : chaque trame vidéo est passée à un
   **packetiseur FFmpeg réutilisable** qui rend les paquets RTP un par un ;
   chacun est converti en `ast_frame` puis `ast_write`.
3. Ce packetiseur est un **composant medkit autonome** (asterisk-indépendant),
   donc **réutilisable** par le mediaserver (logique similaire à étudier).

#### Le composant : `FfRtpPacketizer` (nouveau, medkit)

- **Construction** (1 par piste vidéo, créé à `OpenTrack`) : à partir du codec
  + paramètres (`extradata` avcC, MTU). En interne :
  `avformat_alloc_output_context2(&rtpctx, NULL, "rtp", NULL)`, un stream calqué
  sur le `codecpar`, `AVIOContext` custom en écriture avec
  `max_packet_size = MTU`, `avformat_write_header`.
- **API** : `Packetize(const VideoFrame& f, Sink& out)` — construit un
  `AVPacket` (= données de `f`), `av_write_frame` → le callback AVIO est appelé
  **N fois = N paquets RTP**. Pour chacun : retirer l'en-tête RTP 12 o,
  extraire le bit marqueur (octet 1, `0x80`), pousser `(payload, len, mark)`
  dans `out`.
- **Côté `Mp4PlayerPlayNextFrame`** : pour chaque `(payload, len, mark)`,
  fabriquer un `ast_frame` (format = codec, `subclass |= mark ? 1 : 0`) et
  `ast_write`. Remplace, pour la vidéo, la boucle actuelle
  `MediaFrameToAstFrame2` (`mp4format.cpp:493-522`).

#### Points techniques à valider en revue

- **AVCC vs AnnexB** : `av_read_frame` rend du H264 en **AVCC** (longueurs
  préfixées). Le packetiseur H264 de FFmpeg attend de l'**AnnexB** → intercaler
  le BSF `h264_mp4toannexb`, **ou** stocker la trame en AnnexB. Comme medkit
  sait décoder les deux (`useStartCode`/`naluSizeLen`), choisir une seule
  représentation pour le buffer et convertir au besoin pour le packetiseur.
- **SPS/PPS** : la trame élémentaire doit rester **auto-suffisante pour le
  décodeur** → on **garde** `ReadH264Params`/`PrependWithFrame` (réécrits pour
  lire l'avcC depuis `codecpar->extradata`) qui préfixent SPS/PPS aux trames
  intra. Le packetiseur FFmpeg fragmente alors ces NAL SPS/PPS comme les
  autres. **Veiller à ne pas dupliquer** les SPS/PPS (ne pas compter en plus
  sur l'émission auto par l'`extradata` du muxer RTP).
- **Granularité** : confirmer qu'un appel du callback AVIO = exactement un
  paquet RTP (flush par paquet).

#### Bénéfices

- **Objectif atteint** : un nouveau codec vidéo bénéficie **gratuitement** de la
  packetisation FFmpeg, **sans** écrire de packetiseur maison.
- La `VideoFrame` reste **décodable** (transcodage / MCU).
- **Aucun changement** de `media.h` ni des consommateurs autres que la branche
  vidéo de `Mp4PlayerPlayNextFrame`.
- Comble de fait le trou `PacketizeH263` (jamais défini) sans rien coder.
- Composant **mutualisable** avec le mediaserver.

#### Audio / texte

Inchangés : l'audio (G711/AMR/AAC) tient en un paquet (la branche audio de
`Mp4PlayerPlayNextFrame` reste sur `MediaFrameToAstFrame2`) ; le texte
(T140/RED) garde sa logique RED/RTT existante dans `GetNextFrame`. Le
packetiseur FFmpeg ne concerne **que la vidéo**.

### 4.4 `Mp4Basetrack` et dérivées — réécriture

`Mp4Basetrack` garde son interface (`Create`, `ProcessFrame`, `ReadFrame`,
`GetNextFrameTime`, `Reset`, `IsOpen`, `IsEmpty`, durée…). En interne :

| Champ actuel | Remplacement |
|---|---|
| `MP4FileHandle mp4` | `AVFormatContext * fmt` |
| `MP4TrackId mediatrack` | `int streamIndex` |
| `MP4TrackId hinttrack` | **supprimé** |
| `sampleId` | conservé pour le séquencement logique |
| `timeScale` | `AVRational time_base` du stream |

#### `Mp4AudioTrack`
- **Create** : `avformat_new_stream`, `codecpar->codec_id` =
  `AV_CODEC_ID_PCM_ALAW`/`PCM_MULAW`/`PCM_S16BE`/`AMR_NB`/`AAC`,
  `codec_type=AUDIO`, `sample_rate`, `ch_layout` mono. Pour AAC :
  `codecpar->extradata` = `AACSpecificConfig` (remplace
  `MP4SetTrackESConfiguration`). **Plus de hint track.**
- **ProcessFrame** : `AVPacket` (data = frame medkit), `pts`/`duration`
  calculés depuis l'horloge medkit (mêmes formules qu'aujourd'hui),
  `av_interleaved_write_frame`. Logique de délai initial (silence) conservée.

#### `Mp4VideoTrack`
- **Create** : stream `AV_CODEC_ID_H263`/`H264`, `width`/`height`, time_base
  1/90000. **Plus de hint track.**
- **SPS/PPS (écriture)** : construire l'`extradata` au format **avcC** à partir
  des SPS/PPS détectés (la détection actuelle dans `DoWritePrevFrame`, qui
  itère `RtpPacketizationInfo` pour repérer NAL 0x07/0x08, est conservée mais
  **découplée** de l'écriture hint qui disparaît). MAJ width/height idem.
- **ProcessFrame** : même logique « écrire la trame précédente quand on connaît
  la suivante pour la durée », `pkt->flags |= AV_PKT_FLAG_KEY` si intra.
- **ReadFrame** : retourne une trame **élémentaire**, **sans** appeler
  `Packetize` (la packetisation est différée à l'émission, §4.3).
  `ReadH264Params` / `PrependWithFrame` **conservés** (réécrits pour lire l'avcC
  depuis `codecpar->extradata`) afin que la trame intra reste **auto-suffisante
  pour le décodeur** (transcodage / MCU).

#### `Mp4TextTrack`
- **Create** : stream sous-titre `AV_CODEC_ID_MOV_TEXT` (tx3g, équivalent de
  `MP4AddSubtitleTrack`). Vérifier que le muxer mov accepte le **même format
  d'échantillon** (préfixe 2 octets de longueur + UTF-8) qu'aujourd'hui ;
  sinon adapter (dé)sérialisation dans `ProcessFrame`/`ReadFrame`. Point de
  revue.
- Logique d'encodage incrémental (`TextEncoder`, `SubtitleToRtt`,
  `MAX_SUBTITLE_DURATION`, écriture du `.txt` VM) **inchangée**.
- Pas de packetisation FFmpeg pour le texte (T140/RED géré par medkit).

#### Lecture pilotée par `av_read_frame`
mp4v2 permet l'accès aléatoire par `(trackId, sampleId)` ; libavformat lit en
flux entrelacé via `av_read_frame` (un `AVPacket`, tous streams confondus).

**Approche retenue — file d'attente par stream.** Une petite couche lit en
avance via `av_read_frame` et distribue les `AVPacket` dans une file par
`streamIndex`. `Mp4*Track::ReadFrame()`/`GetNextFrameTime()` consomment leur
file. S'accorde avec l'ordonnanceur de `mp4player` (`GetNextTrackAndTs`).
`Rewind()` → `av_seek_frame(0)` + purge des files. `Reset()` réinitialise le
séquencement logique.

### 4.5 `mp4recorder` / `mp4player` — changements minimes

- Remplacer `MP4FileHandle` par `AVFormatContext*` dans les signatures.
- `mp4recorder` : déclarer la piste audio à la création (§3.3) ; supprimer
  l'auto-création ; `write_header` différé à la 1ʳᵉ trame ; `av_dict_set` +
  `av_write_trailer` au destructeur (§3.4).
- `mp4player::OpenTrack(...)` : l'énumération par hint
  (`MP4FindTrackId(MP4_HINT_TRACK_TYPE)`) est remplacée par un parcours des
  `fmt->streams[]` filtrés par `codec_type` + correspondance codec via
  `codecpar->codec_id`. **Attention** : aujourd'hui le `OpenTrack` vidéo ne
  cherche **que** des pistes hint et n'a **aucun repli** sur piste média — il
  faut donc ajouter le chemin de sélection vidéo « par stream » (l'audio a déjà
  un repli `audio_track_loop2` à promouvoir). La préférence de codec
  (`prefCodec`) et la liste `outputCodecs` sont conservées.
- `mp4player` possède un `FfRtpPacketizer` par piste vidéo (créé à
  `OpenTrack`, §4.3) ; `GetNextFrame` retourne la vidéo **non packetisée**.
- `Mp4PlayerPlayNextFrame` (`mp4format.cpp`) : **branche vidéo** réécrite — la
  trame passe par le `FfRtpPacketizer`, chaque paquet RTP → `ast_frame` →
  `ast_write` ; **branches audio/texte inchangées** (`MediaFrameToAstFrame2`).
- Tout le reste (délais, prologue vidéo `PictureStreamer`, RED/RTT, EOF,
  ordonnancement `GetNextFrame`) **inchangé**.

---

## 5. Phases de réalisation

### Phase 1 — Tests unitaires de référence (sur l'implémentation mp4v2 actuelle)

But : figer le comportement **avant** migration pour mesurer la non-régression.

1. **Trouver / produire un fichier MP4 d'exemple** contenant 3 pistes : 1 audio,
   1 vidéo, 1 sous-titre.
   - Option A : récupérer un enregistrement réel produit par le mediaserver.
   - Option B : le **générer** avec l'implémentation actuelle (pousser des
     `MediaFrame` synthétiques dans `mp4recorder`). Recommandé : reproductible
     et committable comme *fixture*.
   - Stocker sous `libmedikit/test/fixtures/sample_avt.mp4`.
   - utiliser en second fichier de test https://github.com/InteractiviteVideoEtSystemes/fontventa/blob/main/PUB_Guerlain_-_La_petite_robe_noire.mp4
   - le stocker sous `libmedikit/test/fixtures/guerlin_av.mp4`.
2. **Harnais de test** `libmedikit/test/test_mp4_roundtrip.cpp` (compilable
   `ASTERISK=no`) qui, avec l'impl **actuelle** :
   - **lit** la fixture, **énumère** les pistes (type, codec, nb échantillons,
     durée totale) et écrit un **rapport de référence** `reference.txt` : pour
     chaque piste, `(index, ts_ms, durée, taille, sync)` par trame + empreinte
     (taille + hash court). **Rapport au niveau média** (pas hint), pour rester
     comparable après suppression des hint.
   - **réenregistre** : relit chaque trame, la repousse dans un `mp4recorder`
     vers `out_actuel.mp4` ; reparcourt et reproduit le rapport.
   - Affirmations : nb pistes, codecs, ordre/timing cohérents (tolérance sur les
     durées recalculées documentée).
   - fait la même chose avec `libmedikit/test/fixtures/guerlin_av.mp4` dans `reference2.txt`
3. `reference.txt` et `reference2.txt` devienent l'**oracle** de non-régression.

> Livrable Phase 1 : fixture + `test_mp4_roundtrip` + `reference.txt` + `reference2.txt` tournant
> contre mp4v2. Aucune modification du code de prod.

### Phase 2 — Conception détaillée (ce document, à compléter et faire relire)

- Trancher les **points de revue** restants (§6).
- **Revue manuelle E. Buu** avant d'écrire la moindre ligne de prod : fait.

### Phase 3 — Implémentation et vérification de non-régression

1. Adapter `app_mp4.c` (et décider du sort des outils `tools/*`, `mp4creator/`)
   au nouveau contrat : ouverture/fermeture libavformat (§3.2), gestion des
   codes d'erreur (§3.5).
2. Modifier les en-têtes publics : `MP4FileHandle` → `AVFormatContext*` (§3.1).
3. Réécrire `mp4track.{h,cpp}` : pistes libavformat + lecture par file d'attente
   (§4.4). Supprimer tout le code hint. **Conserver**
   `ReadH264Params`/`PrependWithFrame` (réécrits sur `extradata`). La vidéo est
   retournée **non packetisée**.
4. Créer le composant `FfRtpPacketizer` (medkit autonome, §4.3) et réécrire la
   **branche vidéo** de `Mp4PlayerPlayNextFrame` (paquets RTP → `ast_frame`).
5. Adapter `mp4recorder` (déclaration audio en amont, header différé, trailer,
   métadonnées) et `mp4player` (sélection des pistes par stream, possession du
   `FfRtpPacketizer` par piste vidéo) — §4.5.
6. Mettre à jour le `Makefile` : retirer `-I…/staticdeps/include` (mp4v2) et la
   lib mp4v2 du link ; vérifier `-lavformat` (déjà dans `LDFLAGS`).
7. Rejouer **le harnais Phase 1** contre la nouvelle implémentation : comparer
   au `reference.txt`. Documenter/justifier chaque écart attendu (timing
   recalculé, packetisation RTP issue de FFmpeg, ordre d'entrelacement).
8. Tests bout en bout : lecture d'un enregistrement réel via `app_mp4` /
   mediaserver (audio audible, vidéo décodable, sous-titres affichés) ; vérifier
   que les **anciens** fichiers (avec hint mp4v2) restent **lisibles** par la
   nouvelle implémentation (libavformat ignore les pistes hint).

> Livrable Phase 3 : `libmedikit` sans aucune référence à `mp4v2.h`, harnais de
> non-régression vert, validation manuelle d'un appel réel.

---

## 6. Risques et points ouverts (synthèse pour la revue)

| # | Risque / question | Décision / proposition | À valider |
|---|---|---|---|
| 1 | `GetNextFrame` retourne la vidéo **sans** packetisation (option D) | Vérifier que tous les consommateurs de `mp4player::GetNextFrame` packetisent à l'émission (pour libmedikit : seul `Mp4PlayerPlayNextFrame`, branche vidéo réécrite) | ☐ |
| 2 | SPS/PPS : la trame doit rester décodable | `ReadH264Params`/`PrependWithFrame` **conservés** (avcC depuis `extradata`) ; veiller à **ne pas dupliquer** SPS/PPS avec l'émission auto du muxer RTP | ☐ |
| 3 | Entrée mp4 en AVCC vs packetiseur H264 FFmpeg attendant de l'AnnexB | BSF `h264_mp4toannexb` ou buffer stocké en AnnexB ; une seule représentation pour le buffer | ☐ |
| 4 | Granularité : un callback AVIO = exactement un paquet RTP (flush) | À valider à l'implémentation | ☐ |
| 5 | `FfRtpPacketizer` doit être réutilisable par le mediaserver | Composant medkit autonome (asterisk-indépendant) | ☐ |
| 6 | **Plus d'auto-création paresseuse** : audio déclaré en amont, header différé | Déclarer audio dans `Mp4RecorderCreate`, supprimer auto-création | ☐ |
| 7 | Format échantillon sous-titres tx3g (préfixe longueur) vs `mov_text` | Vérifier compat | ☐ |
| 8 | Compatibilité **lecture** des anciens MP4 (avec hint) | libavformat lit la piste média et ignore le hint → OK *a priori* | ☐ |
| 9 | Outils `tools/*`, `mp4creator/`, partie C de `app_mp4` | Porter au nouveau contrat ou abandonner (debug) | ☐ |
| 10 | `mcu/src/mp4*.cpp` (autre impl mp4v2) non traité | Migration ultérieure, même méthode | ☐ |

---

## 7. Références code

- `mp4track.cpp` — tout l'usage mp4v2 (création/écriture/lecture, hint, SPS/PPS).
- `mp4track.cpp:130-206` (`PacketizeH264*`) ; `video.cpp:9-24` (`Packetize`, `PacketizeH263` **non définie**).
- `media.h:42-49,113-135` — modèle `RtpPacketization` / `RtpPacketizationInfo`.
- `mp4recorder.cpp:57-91` — tags `MP4Tags*` dans le destructeur (→ §3.4).
- `mp4player.cpp:206-323` — `OpenTrack` vidéo (hint uniquement, **sans repli média**).
- `mp4player.cpp:419-556` — `GetNextFrame` (ordonnanceur, repli `Packetize`).
- `mp4format.cpp:273-310` — `Mp4RecorderCreate` (déclaration des pistes).
- `mp4format.cpp:467-535` — `Mp4PlayerPlayNextFrame` (consomme `RtpPacketizationInfo`).
- `app_mp4/app_mp4.c:387,644` ; `tools/*.cpp` — ouverture/fermeture du fichier.
- Briques ffmpeg déjà en place : `ffaudiocodec.{h,cpp}`, `ffvideocodec.{h,cpp}`.
