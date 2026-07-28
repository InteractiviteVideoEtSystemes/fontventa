# CLAUDE.md

Ce fichier guide Claude Code (claude.ai/code) dans ce dépôt.

## Ce qu'est ce dépôt

Dépôt **fontventa** : bibliothèques et modules média d'origine Fontventa / i6net,
maintenus par IVèS. Il contient deux ensembles très différents en niveau
d'activité :

1. **`libmedikit/`** — bibliothèque média C++ **autonome** (`libmedkit.a`) :
   codecs audio/vidéo/texte adossés à ffmpeg, lecture/écriture MP4,
   packetisation RTP, négociation de codecs/`fmtp`, texte T.140/RED, outils
   bitstream. C'est la partie **activement maintenue**, portée
   **AlmaLinux 9 / GCC 11 / ffmpeg 5**. Elle est consommée soit par les modules
   Asterisk de ce dépôt, soit par un projet applicatif externe qui l'embarque en
   sous-module et lie `libmedkit.a` par chemin.
2. **Modules Asterisk et outils historiques** — `app_mp4/` (`mp4save`/`mp4play`),
   `app_rtsp/`, `app_transcoder/`, `astlog/`,
   `mp4av/` + `mp4creator/` (fork mpeg4ip), `tools/` (`mp4asterisk`, `mp4band`,
   `pcm2mp4`, `IVES_convert.ksh`). Code ancien, peu touché ; il exige les
   en-têtes `<asterisk/...>` d'`asteriskv-devel`.

Branche de travail : **`migration/almalinux_9`**. Commentaires et messages de
commit majoritairement **en français**.

## Build

### libmedikit (le cas courant)

```sh
cd libmedikit
make ASTERISK=no            # -> libmedkit.a, sans les objets couplés Asterisk
make ASTERISK=no check      # build + exécution de la suite gtest
make clean
```

Commutateurs de `libmedikit/Makefile` :

- **`ASTERISK`** (défaut `yes`) — inclut `transcoder.o`, `mp4format.o`,
  `framebuffer.o`, `frameutils.o`, `astlog.o`, qui exigent `<asterisk/...>`
  (chemin `CUSTOM_ASTPATH`). **`ASTERISK=no` est le mode normal** hors contexte
  Asterisk : sans lui, le build casse faute d'en-têtes Asterisk.
- **`DEBUG`** (défaut `yes`) — pas de `-O3` (`-g` est toujours passé).
- **`LOG`** (défaut `yes`) — `-DLOG_`, active `Debug()`.
- **`FLV1PARSER`** — vestige : il définit `-DFLV1PARSER` et des variables
  `FLV1DIR`/`FLV1OBJ` qui ne sont **plus référencées** par `OBJS` (le répertoire
  `flv1/` n'existe pas). Sans effet.

Dépendances : ffmpeg 5 (`-I/usr/include/ffmpeg` ; `avcodec`, `avformat`,
`avutil`, `swscale`, `swresample`), `x264`, `openssl`, `bz2`, et **`mp4v2` +
`gsm`** liés seulement par les exécutables (`tests/runtests`, `ffmp4probe`,
`negotest`). `mp4v2` n'étant pas packagé, il est attendu en **statique** sous
`../../../staticdeps/{include,lib}` — fourni par le projet hôte qui embarque ce
dépôt. En build isolé, il faut donc produire cet arbre ou ajuster
`INCLUDE`/`-L` dans le Makefile.

Cibles utiles : `all` (= `libmedkit.a`), `check`/`tests`, `ffmp4probe` (harnais
de lecture MP4 hors-ligne : ouverture, métadonnées, lecture cadencée, seek),
`negotest` (harnais du négociateur), `install`/`uninstall` (en-têtes `medkit/` +
`astmedkit/` et `libmedkit.a` sous `/opt/ives`).

> **Piège majeur** : le Makefile **ne suit pas les dépendances d'en-têtes**
> (seules trois règles explicites existent en fin de fichier). Après toute
> modification d'un `.h`, faire `make clean` (ou `rm *.o`) avant de reconstruire,
> sinon on obtient des objets incohérents (corruption silencieuse au lien).

### Modules Asterisk (`Makefile` racine)

```sh
./install.ksh prereq     # deps: ffmpeg-devel mp4v2-devel asteriskv-devel SDL-devel x264-devel
./install.ksh rpm nosign # rpmbuild via fontventa.spec (omettre "nosign" pour signer GPG)
./install.ksh clean
```

`install.ksh` **génère `Makeinclude`** (`SYS_LIB=`) que le `Makefile` racine
inclut : le lancer au moins une fois avant tout `make` à la racine. Le `Makefile`
racine bâtit `libmedkit.a`, puis `app_mp4.so`, `app_rtsp.so`, `astlog`,
`mp4creator`. Les modules se lient contre `libmedkit.a` en statique
(`-L../libmedikit -lmedkit -lmp4v2`) et s'installent dans
`$(libdir)/asterisk/modules`.

Deux specs distinctes : `fontventa.spec` (modules `app_*.so` + outils,
version 1.6.17) et `libmedkit.spec` (la seule lib + ses en-têtes sous
`/opt/ives`, version 2.0.0). `build.bash` est **obsolète** (il force
ffmpeg 0.4.2). `medkit/version.h` (`MCUVERSION "1.5.1"`) est **périmé** : la
version réelle vit dans les specs.

## Tests (`libmedikit/tests/`)

Suite **GoogleTest** (paquet système `gtest-devel`, détecté par `pkg-config`) —
`make check` depuis `libmedikit/`. Voir `libmedikit/tests/README.md` pour le
détail des suites et des fixtures. Points structurants :

- La cible force un **sous-make `ASTERISK=no`** pour (re)construire
  `libmedkit.a` : ne jamais lancer les tests après un build `ASTERISK=yes`.
- **`SetLogFunctions` est obligatoire** : `libmedkit` appelle `Log()`/`Error()`
  via des pointeurs de fonction ; sans initialisation → segfault. C'est fait une
  fois dans un `::testing::Environment` global (`tests/test_env.cpp`).
- `gtest_main` fournit `main()` : ne pas écrire de `main()`, et ne pas lier une
  bibliothèque qui en apporte un.
- Fixtures **versionnées** (`tests/fixtures/*.mp4`), chemins injectés à la
  compilation (`-DTEST_MP4_FILE`, `-DTEST_MP4_TITI_FILE`, surchargeables).
  `record.mp4` a un **flux H264 défectueux** (ffmpeg lui-même le rejette) : il ne
  sert qu'aux métadonnées et au contrat AAC ; la lecture vidéo réelle se teste
  sur `titi.mp4`.
- Le test VAAPI (`H264HwVaapi`) est **`DISABLED_`** : il exige un GPU
  (`/dev/dri/renderD128`) et échoue volontairement sans. Le lancer avec
  `--gtest_also_run_disabled_tests --gtest_filter='*H264HwVaapi*'`.
- Convention : chaque module a ses tests **nominaux et adverses** (entrées
  malformées). Les tests adverses ont plusieurs fois motivé un **durcissement du
  code de production** (`h264/h264.h`, `red.cpp`) — refuser proprement une entrée
  invalide plutôt que lire hors limites. Garder ce réflexe.
- **Piège d'inclusion** : `h264/h264.h` utilise `Debug()`/`Log()` → inclure
  `medkit/log.h` **avant**.

## Architecture de libmedikit

`medkit/` = en-têtes publics **sans dépendance Asterisk** ; `astmedkit/` = liant
Asterisk. Les `.cpp` sont à plat à la racine de `libmedikit/`, sauf les codecs
regroupés par famille (`h264/`, `vp8/`, `av1/`, `aac/`, `amr/`, `opus/`,
`speex/`, `gsm/`, `g711/`, `g722/`, `h263/`, `nelly/`), atteints via `VPATH`.

### Modèle de trame

`MediaFrame` (`medkit/media.h`) : type (Audio/Video/Text/Application), buffer
(possédé ou non — `ownsbuffer`), timestamp, durée, et une
**`RtpPacketizationInfo`** = liste de `RtpPacketization` décrivant chaque paquet
RTP par `pos`/`size` (tranche du buffer) + `prefix[16]`/`prefixLen` + `mark` ;
soit `payload = prefix ++ buffer[pos..pos+size]`. `Packetize(mtu)` remplit cette
liste. Dérivées : `AudioFrame`, `VideoFrame` (intra, dimensions,
`SetH264NalSizeLength()` pour lire de l'AVCC ou de l'Annex-B), `TextFrame`.

Ce contrat est la **frontière d'ABI** de la bibliothèque : voir « Conventions ».

### Codecs

- `medkit/codecs.h` : énumérations `AudioCodec::Type`, `VideoCodec::Type`,
  `TextCodec::Type`, `AppCodec::Type` (**les valeurs sont des payload types /
  identifiants d'API : ne pas les changer**), + `GetNameFor`/`GetCodecFor`.
- **Capacités** : `*Codec::IsSupported(type)` et
  `*CodecFactory::GetSupportedCodecs()` — « supporté » = ffmpeg compilé avec ce
  codec, testé par `avcodec_find_encoder(_by_name)`/`avcodec_find_decoder`, donc
  le **même test qu'à l'ouverture réelle**. Catalogue calculé une fois et
  mémoïsé ; l'ordre du vecteur = ordre de préférence. `IsSupported` est défini
  dans `codecs.cpp` pour garder l'en-tête sans ffmpeg.
- **Fabriques** : `AudioCodecFactory`/`VideoCodecFactory::CreateEncoder/Decoder`,
  avec surcharge `Properties` (configuration) et, pour l'audio, surcharge
  `extradata` (`AudioSpecificConfig`/`esds`, requise par l'AAC des MP4).
- **Socle ffmpeg** : `FfAudioEncoder`/`FfAudioDecoder`
  (`ffaudiocodec.{h,cpp}`, + libswresample pour le rééchantillonnage) et
  `FfVideoEncoder`/`FfVideoDecoder` (`ffvideocodec.{h,cpp}`). Les classes par
  codec en dérivent ou les enveloppent. Mapping `AVCodecID ↔ *Codec::Type` par
  `MapVideoCodec`/`MapAudioCodec`.
  - `FfVideoEncoder` sait tenter un **encodeur VAAPI** (`tryHW`) avec repli
    logiciel, ou forcer un nom d'encodeur (`codec_name`, ex. `libsvtav1` plutôt
    que `libaom-av1`). Un `AVCodecContext` ne se rouvrant pas, la
    reconfiguration à chaud passe par `CloseCodec`/`ReopenCodec` en conservant le
    device VAAPI. Propriété `video.hwaccel.required=1` = **pas de repli
    logiciel** ; `IsHardwareReady()` renseigne l'appelant.
  - `FfAudioEncoder` **n'accumule pas** les échantillons pour l'appelant :
    l'ordre de vie attendu est construction → réglages du dérivé → `TrySetRate()`
    (fixe format/fréquence et crée le resampler S16→natif) → `Open()`
    (`frame_size` connu seulement après).
- Codecs présents : H264 (x264/VAAPI, décodeur ffmpeg, dépacketiseur RTP),
  H263-1996/1998 + MPEG4, VP8 (décodeur natif ffmpeg, encodeur via le wrapper
  libvpx), AV1 (libsvtav1 / libdav1d), AAC, AMR, Opus, Speex, GSM, G711
  (PCMU/PCMA, tables), G722, Nelly.
- Propriétés de configuration reconnues (clés `Properties`) :
  `h264.profile-level-id`, `h264.intra_refresh`, `h264.qpel`,
  `opus.useinbandfec`, `opus.usedtx`, `opus.maxaveragebitrate`, `opus.cbr`,
  `opus.packet-loss-perc`, `vp8.max-fr`, `vp8.max-fs`, `av1.profile`,
  `av1.level-idx`, `av1.tier`, `av1.preset`, `aac.bitrate`, `aac.samplerate`,
  `speex.quality`, `video.hwaccel.required`.

### Négociation de codecs et `fmtp`

`medkit/negotiator.h` — `CodecNegotiator::Negotiate(media, proposed, localProps,
remoteFmtp, out)`. Composant **sans dépendance au projet appelant** :

1. il **intersecte** la `RTPMap` proposée avec les codecs réellement supportés —
   un PT non supporté **disparaît** de `acceptedMap` ;
2. pour chaque PT retenu, il **dérive le `fmtp` local** des `Properties` +
   défauts, **sans ouvrir de codec** ;
3. le `fmtp` produit contient les **paramètres seuls** (pas de préfixe
   `a=fmtp:<pt> `) ; un codec sans paramètre rend une chaîne **vide**.

`fmtp` générés : H264 `profile-level-id=<id>;packetization-mode=1` (défaut
`42801F`, émis en minuscules) ; Opus (paramètres non nuls concaténés, mêmes clés
et défauts que le constructeur de l'encodeur, pour éviter toute dérive) ; VP8
`max-fr`/`max-fs` ; AV1 `profile`/`level-idx`/`tier` ; T140RED via
`TextCodec::GetT140RedFmtpParams(t140Pt, generations=3)` → `98/98/98` (produit
seulement si un T140 est aussi proposé **et** supporté). Aucun `fmtp` pour PCMU,
PCMA, G722, GSM, AAC, AMR, Speex, Nelly, T140.

> **Le `fmtp` de négociation est dérivé de la configuration, pas d'un encodeur
> chaud.** En particulier `sprop-parameter-sets` (SPS/PPS H264) est
> **délibérément absent** : il n'existe qu'après l'encodage d'une première trame.
> `*Encoder::GetFmtpInfo()` (chemin « encodeur ouvert ») est un mécanisme
> distinct, à ne pas confondre avec la génération de négociation.

L'ingestion du `fmtp` **distant** est prévue dans la signature (`remoteFmtp`)
mais **ignorée** en l'état ; `effectiveProps` vaut donc `localProps`.

### MP4

- **Écriture** — `medkit/mp4writer.h` (`mp4writer`), sur **mp4v2**. Pistes
  déclarées explicitement par `AddTrack()` (audio / vidéo / texte), puis
  `ProcessFrame()` par trame (codes de retour détaillés dans l'en-tête).
  Fonctions annexes : attente de la première intra (`waitVideo`,
  `IsVideoStarted()`), **prologue vidéo** en trames noires
  (`EnableVideoPrologue`, via `PictureStreamer`), délai initial
  (`SetInitialDelay`) pour un participant arrivé en cours de route, nom de piste,
  texte des sous-titres recopié dans le tag `comment`.
  - **Piège mp4v2** : le `mp4writer` écrit encore dans son destructeur
    (`MP4TagsStore`) → **le détruire avant `MP4Close()`**, sinon assertion mp4v2
    (`AddDescendantAtoms`).
- **Lecture** — `medkit/ffmp4reader.h` (`Mp4FfReader`), sur
  **ffmpeg/libavformat**. Remplace le lecteur historique mp4v2 piloté par les
  *hint tracks* : il démuxe n'importe quel MP4, **hinté ou non**. Décisions de
  conception à respecter :
  - le lecteur **ouvre le fichier lui-même** (`avformat_open_input` depuis un
    chemin) ; on ne lui passe pas de handle déjà ouvert ;
  - `GetNextFrame(errcode, waittime)` rend une `MediaFrame` **déjà packetisée
    RTP** (contrat historique conservé, les appelants itèrent
    `GetRtpPacketizationInfo()`) ;
  - la packetisation utilise le packetiseur maison (`VideoFrame::Packetize`), pas
    de bitstream filter : on lit l'échantillon **AVCC** tel quel, on règle
    `SetH264NalSizeLength(nalLengthSize)` depuis l'`avcC` et on **préfixe
    SPS/PPS sur les intra** pour que la trame reste auto-suffisante ;
  - cadencement par `dts` (normalisé sur le premier), estampille par `pts` ;
    `Seek`/`PreSeek`/`Rewind`/`Tell` ;
  - **passthrough** par défaut (H264/H263/VP8 ; PCMU/PCMA/AMR/G722/Opus).
    `OpenAudioTranscoded(target)` ajoute le repli **décodage → resampling
    (swresample) → réencodage par tranches de 20 ms**, notamment pour lire une
    piste AAC vers un pair télécom.
  - `HasAudioCodec`/`HasVideoCodec` interrogent le fichier **sans effet de bord**
    (la piste sélectionnée ne change pas) : utile à la négociation côté appelant.
- `libmedikit/supp_mp4v2.md` est un **brouillon non implémenté** de suppression
  totale de mp4v2 (écriture comprise). Attention : sa « décision » de
  packetisation différée (option D, `FfRtpPacketizer`) a été **écartée** pour la
  lecture, qui conserve le contrat packetisé ci-dessus. Ne pas le lire comme
  l'état du code.

### Texte et divers

- Texte : `medkit/text.h` (`UTF8Parser`, `TextFrame`),
  `medkit/text2subtitle.h` (accumulation RTT ↔ sous-titres, `SubtitleToRtt`),
  `medkit/red.h` (`RTPRedundantPayload`/`RTPRedundantEncoder`, RFC 2198/4103).
- Outils bas niveau : `medkit/bitstream.h`, `medkit/tools.h`,
  `medkit/avcdescriptor.h` (`avcC`, `H264SeqParameterSet`),
  `medkit/h263packet.h`, `medkit/framescaler.h`, `medkit/logo.h` +
  `medkit/picturestreamer.h` (image fixe → flux encodé), `medkit/stunmessage.h`,
  `medkit/fifo.h`, `medkit/audiosilence.h`, `medkit/log.h` (`SetLogFunctions`),
  `medkit/config.h` (typedefs `BYTE`/`WORD`/`DWORD`/`QWORD`, tailles d'image
  `CIF`/`VGA`/…, `MTU`, classe `Properties`).
- `astmedkit/` (uniquement `ASTERISK=yes`) : `mp4format.h` — `AstMp4Recorder`
  (dérive `mp4writer`, ajoute `ProcessFrame(ast_frame*)`) et l'API C
  `Mp4RecorderCreate/Frame/Destroy`, `Mp4PlayerCreate/PlayNextFrame/Destroy`
  consommée par `app_mp4` ; `framebuffer.h` (`AstFrameBuffer`, jitter buffer
  d'`ast_frame`) ; `frameutils.h` ; `astlog.h`. `medkit/transcoder.h`
  (`VideoTranscoder*`, API C sur `ast_frame`) est également couplé Asterisk.

## Conventions et pièges

- **C++ ancien** : pas de `-std` imposé (seul `framebuffer.o` force `-std=c++0x`),
  gestion mémoire manuelle, threads/mutex POSIX, `std::wstring` pour le texte,
  typedefs maison de `config.h`. Rester dans le style du fichier voisin.
- **Français** pour les commentaires et les messages de commit.
- **Fins de ligne LF** — ne jamais introduire de CRLF (piège connu du dépôt).
- **Stabilité d'ABI** : les en-têtes `medkit/*.h` sont l'interface unique vue par
  les projets qui lient `libmedkit.a` **par chemin**, parfois en coexistant avec
  leurs propres copies d'en-têtes homonymes. Une divergence de *layout* ne
  produit **aucune erreur de compilation**, seulement de la corruption mémoire
  silencieuse. Donc : ne pas réordonner les membres de
  `MediaFrame`/`VideoFrame`/`AudioFrame`, ne pas insérer de méthode virtuelle
  ailleurs qu'en fin de vtable (`VideoDecoder::DecodePacket` est
  **volontairement** placée entre `Decode` et `GetFrame` pour rester alignée avec
  les consommateurs), et ne pas changer les valeurs des énumérations de
  `codecs.h`. Toute modification de ce genre se coordonne avec les projets
  consommateurs.
- Nouveaux objets : ajouter le `.o` aux `OBJS` du Makefile et, si le source est
  dans un sous-répertoire de codec, la ligne `VPATH` correspondante.
- Un codec ajouté doit être déclaré **partout** : fabrique
  (`Create{En,De}coder`), `IsSupported`, `GetSupportedCodecs`, `GetNameFor` /
  `GetCodecFor`, et le cas échéant génération de `fmtp` + tests.
- Les objets couplés Asterisk restent **hors périmètre** des builds et tests
  courants (`ASTERISK=no`).
