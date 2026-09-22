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
   **AlmaLinux 9 / GCC 11 / ffmpeg 9**. Elle est consommée soit par les modules
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

Dépendances : ffmpeg 9 du paquet IVèS `ffmpeg-devel` (`avcodec`, `avformat`,
`avutil`, `swscale`, `swresample`, `avfilter`), trouvé **uniquement par
`pkg-config`** (`PKG_CONFIG_PATH` pour un ffmpeg hors préfixe système ; le
Makefile s'arrête en erreur sinon), `openssl`, `bz2`, et `mp4v2` lié seulement
par les exécutables (`tests/runtests`, `ffmp4probe`, `negotest`). `mp4v2-devel`
système suffit ; `MP4V2INC` pointe encore vers `../../../staticdeps/include`
pour le projet hôte qui embarque ce dépôt avec ses dépendances statiques. Les
codecs externes (x264, libvpx, opus, speex, gsm, AMR) sont **embarqués en
statique dans `libavcodec`** par le paquet IVèS : rien à lier en plus.
Voir `docs/reference/ffmpeg.md`.

Cibles utiles : `all` (= `libmedkit.a`), `check`/`tests`, `ffmp4probe` (harnais
de lecture MP4 hors-ligne : ouverture, métadonnées, lecture cadencée, seek),
`negotest` (harnais du négociateur), `install`/`uninstall` (en-têtes `medkit/` +
`astmedkit/` et `libmedkit.a` sous `/opt/ives`).

> **Piège** : le Makefile suit les dépendances d'en-têtes du dépôt (`-MMD -MP`,
> fichiers `.d` inclus), mais **pas les en-têtes système** : après un changement
> de paquet `ffmpeg-devel`, `mp4v2-devel` ou `asteriskv-devel`, faire
> `make clean` avant de reconstruire, sinon on obtient des objets incohérents
> (corruption silencieuse au lien).

### Modules Asterisk (`Makefile` racine)

```sh
./install.ksh prereq     # deps: ffmpeg-devel (>= 9, dépôt ives-externals) mp4v2-devel asteriskv-devel
./install.ksh rpm nosign # rpmbuild via fontventa.spec (omettre "nosign" pour signer GPG)
./install.ksh clean
```

`install.ksh` **génère `Makeinclude`** (`SYS_LIB=`) que le `Makefile` racine
inclut : le lancer au moins une fois avant tout `make` à la racine. Le `Makefile`
racine bâtit `libmedkit.a`, puis `app_mp4.so`, `app_rtsp.so`, `astlog`,
`mp4creator`. Seul `app_mp4` se lie contre `libmedkit.a` (en statique,
`-L../libmedikit -lmedkit`) ; les modules s'installent dans
`$(libdir)/asterisk/modules`.

> **Piège** : `libmedkit.a` existe en deux variantes selon `ASTERISK`, et un
> module lié contre la variante `ASTERISK=no` **se lie sans erreur** (les
> symboles indéfinis sont tolérés dans un objet partagé) — la panne n'apparaît
> qu'au chargement : `undefined symbol: RedirectLogToAsterisk`. C'est ce que
> produit un `make check` (qui force `ASTERISK=no`) suivi d'un `make` dans
> `app_mp4/`. Deux gardes du `app_mp4/Makefile` l'interceptent désormais :
> `check-medkit` (avant le lien : les objets d'`ASTOBJ` sont-ils dans
> l'archive ?) et `check-undefined` (après le lien : `ldd -r` ne doit laisser
> indéfinis que les symboles `ast_*`/`option_*`/`pbx_*`, fournis par le binaire
> asterisk). Les deux se lancent aussi à la main. Si un autre module vient à
> lier `libmedkit.a`, y reprendre ces gardes.
>
> `-fno-gnu-unique` est passé en `ASTERISK=yes` : g++ émet sinon des symboles
> `STB_GNU_UNIQUE` (statiques locales de fonctions inline/templates, ex.
> `std::piecewise_construct` dès qu'on utilise `std::map`), et la glibc marque
> **NODELETE** tout `.so` qui en définit — `dlclose()` rend alors 0 sans jamais
> décharger et la boucle `while (!dlclose(lib));` de `load_dynamic_module`
> (asterisk 1.4) tourne à l'infini au démarrage.

Un seul spec : `fontventa.spec` (modules `app_*.so` + outils). Le paquet
libmedkit séparé (`libmedkit.spec`/`install_lib.ksh`) a été supprimé :
app_conference compile libmedkit depuis le submodule fontventa. La CI est
GitLab CI (`.gitlab-ci.yml`, publication vers `forks-testing`), `build.bash`
(Jenkins) a été supprimé. `medkit/version.h` (`MCUVERSION "1.5.1"`) est
**périmé** : la version réelle vit dans le spec.

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
  - `IsSupported` porte sur le **décodeur** seul ;
    `IsEncodingSupported`/`GetSupportedEncoderCodecs` sur l'encodeur. ffmpeg
    décode des codecs qu'il n'encode pas : un paquet ffmpeg sans
    libopencore-amr, libgsm ou libspeex garde `IsSupported` vrai et casse à la
    création de l'encodeur. `tests/test_codec_catalogue.cpp` le détecte.
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
    logiciel** ; `video.hwaccel=0` = refus explicite du matériel pour cet
    encodeur (`required` reste plus fort) ; `IsHardwareReady()` renseigne
    l'appelant.

### Accélération matérielle : les cinq règles d'allocation

Aucune ne se devine, et **les enfreindre ne produit pas d'erreur** : la vidéo
disparaît en silence, ou le processus meurt. Chacune a coûté une panne réelle.

1. **Une surface VAAPI s'alloue en `AV_PIX_FMT_NV12`, jamais en `YUV420P`.** Le
   driver iHD n'encode pas depuis de l'I420. Il échoue image par image alors
   qu'`avcodec_send_frame` rend toujours 0, donc aucun paquet ne sort jamais, et
   au bout de ~16 images libavcodec meurt sur `pic->nb_dpb_pics < 16`.
   `av_hwframe_transfer_data` convertit une trame CPU YUV420P vers une surface
   NV12 : rien d'autre n'est à faire pour alimenter l'encodeur.
2. **Un buffersrc avfilter reçoit son `hw_frames_ctx` AVANT d'être initialisé.**
   Donc `avfilter_graph_alloc_filter` → `av_buffersrc_parameters_set` →
   `avfilter_init_str`, **jamais** `avfilter_graph_create_filter`, qui initialise
   séance tenante. Sinon : « *Setting BufferSourceContext.pix_fmt to a HW format
   requires hw_frames_ctx to be non-NULL!* », la configuration du graphe échoue,
   et toute mise à l'échelle rend `nullptr` dès que la source décode en matériel.
3. **Un encodeur matériel retient sa première image.** `EncodeFrame` rend alors
   `nullptr` **sans que rien n'aille mal** : ce n'est pas un échec, et un
   appelant qui le traite comme tel n'émet jamais rien. Sur une image FIXE (logo,
   prologue) on représente la même image jusqu'à ce qu'une trame sorte ; sur un
   flux, on passe simplement à l'image suivante.
4. **Ne jamais vider un encodeur qui n'a rien reçu.** `avcodec_send_frame(ctx,
   NULL)` sur un `h264_vaapi` jamais alimenté segfaute dans libavcodec. Cas banal :
   un encodeur ouvert à la négociation, fermé avant la première image
   (`FfVideoEncoder::DrainCodec` s'en garde par son drapeau `fed`).
5. **Un seul device pour tout le processus** — `Pict::GetVAAPIDevice()`.
   Décodeurs, encodeurs, uploads et graphes de composition en dérivent tous par
   `av_buffer_ref` : les filtres `*_vaapi` refusent de mélanger des trames issues
   de devices distincts. `Pict::DisableVAAPI()` l'éteint pour tout le processus,
   et doit être appelée **avant** tout usage média — un device déjà distribué
   survit chez celui qui en tient une référence.

Conséquence pour les tests : un harnais qui suppose « un appel à `EncodeFrame`,
une trame » devient rouge sur toute machine équipée d'un GPU, et mesure alors la
latence de l'encodeur au lieu de son sujet. Un test qui porte sur le rate control
de libx264 (VBV, régimes CRF, consigne à chaud) doit poser `video.hwaccel=0` :
aucun encodeur VAAPI ne reproduit ces propriétés.
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

### Fichiers média (MP4, Matroska)

- **Écriture, chemin historique** — `medkit/mp4writer.h` (`mp4writer`), sur **mp4v2**. Pistes
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
- **Écriture, second chemin** — `medkit/ffmediafilewriter.h`
  (`FfMediaFileWriter`), sur **ffmpeg/libavformat**. Même API de pistes
  (`AddTrack`/`ProcessFrame`, mêmes codes de retour), mais le **conteneur suit
  l'extension** du nom de fichier : `.mp4`, `.mov`, `.3gp`, `.mkv`, `.mka`,
  `.webm`. Les deux classes **coexistent volontairement** : `mp4writer` reste le
  chemin d'Asterisk et de tout appelant qui déclare ses pistes en cours
  d'enregistrement, ce que libavformat ne sait pas faire.
  - **Aucune piste ne naît après la première trame** : libavformat fige l'en-tête
    dès la première écriture. Donc pas d'auto-création (une trame sans piste rend
    -3), et `AddTrack` après coup est refusé. L'en-tête est **différé** jusqu'à
    ce que toutes les pistes soient déclarables ; les trames reçues d'ici là sont
    mises de côté (file bornée), puis écrites.
  - **`ExpectTrack(track, maxWaitMs)` est la seule issue pour un appelant qui ne
    connaît pas encore un codec** — une source RTP ne le livre qu'avec son
    premier paquet, alors que l'audio d'un autre média coule déjà. La piste
    annoncée **retient l'en-tête** jusqu'à son `AddTrack`, au plus `maxWaitMs` ;
    passé ce délai (ou si la file de mise de côté déborde, ou à `Close()`), elle
    est abandonnée et les autres pistes sont sauvées. Sans cette annonce, l'ordre
    d'arrivée des médias décide silencieusement de ce que le fichier contient.
  - **La classe possède le fichier** (ouverture au constructeur, trailer à
    `Close()`, appelé par le destructeur) : plus d'ordre de destruction à
    respecter, contrairement au piège mp4v2 ci-dessus.
  - **H264 et AV1 attendent leurs paramètres** : l'`avcC` est reconstruit
    (`AVCDescriptor`) depuis les SPS/PPS de la première trame AVCC, l'`av1C`
    depuis le sequence header OBU (`AV1ParseObuStream`). Sans eux, aucun des deux
    muxers ne peut écrire son en-tête — d'où le report ci-dessus. Une piste dont
    aucune trame ne porte ces paramètres est **abandonnée**, les autres sont
    enregistrées.
  - **Le délai initial n'est plus comblé par du média synthétique** : la première
    trame porte son horodatage réel (`SetInitialDelay` + temps écoulé depuis
    l'ouverture) et le conteneur exprime le trou — *edit list* en ISOBMFF,
    absence de bloc en Matroska. Ni prologue vidéo ni pré-roll de silence, à la
    différence de `mp4writer`.
  - **PIÈGE : le muxer `mp4` de ffmpeg refuse tous les codecs télécom** — PCMU,
    PCMA, SLIN, AMR, G722, GSM, H263 — quelle que soit la conformité demandée
    (`Could not find tag for codec … in stream #0`). Les enregistrer sans
    transcodage demande `.mkv` (tous) ou `.mov` (tous sauf G722 et Opus, que ce
    muxer refuse aussi ; il refuse VP8 également). `IsCodecSupported(container,
    codec)` répond **avant** d'ouvrir la piste, et la table est prouvée couple par
    couple par `tests/test_ffmediafilewriter.cpp` — ne pas la « corriger » sans
    ce test, `avformat_query_codec` mentant dans les deux sens.
  - Le texte suit le conteneur : `mov_text` (tx3g, `[longueur 2 octets][UTF-8]`)
    en ISOBMFF, `S_TEXT/UTF8` (texte nu) en Matroska. Le muxer ISOBMFF **insère
    ses propres échantillons vides** pour combler les trous d'une piste de
    sous-titres : le premier échantillon relu n'est pas forcément le premier
    écrit.
  - **Le texte est enregistré au fil de l'eau, et ça ne s'obtient pas
    gratuitement.** Chaque frappe produit son échantillon tout de suite, portant
    pour durée l'intervalle qui la PRÉCÈDE (modèle `mp4writer` : l'horloge de la
    piste est la somme des durées écrites, donc les échantillons se suivent sans
    trou). Retenir le sous-titre jusqu'à la frappe suivante donnerait des durées
    exactes, mais perdrait la dernière frappe d'un enregistrement interrompu :
    arbitrage tranché le 2026-09-09, l'écriture immédiate est **impérative**.
    Trois tampons séparent pourtant l'écriture du disque, et il faut les vider
    tous les trois (`FlushToDisk`) : la file d'entrelacement de libavformat, les
    tampons du muxer — **Matroska garde son Cluster courant en mémoire**, seul un
    paquet NULL passé à `av_write_frame` le referme —, puis l'AVIO. Sans le
    deuxième, un `.mkv` interrompu ne contient aucun sous-titre.
  - Le fichier texte annexe (`textfile` d'`AddTrack`) reçoit chaque ligne
    terminée à la frappe du saut de ligne, et `onLineRemoved` la retire quand
    l'utilisateur revient en arrière. **`ftruncate` ne déplace pas la position
    d'écriture** : sans un `lseek` derrière, la ligne suivante atterrit au-delà
    de la fin et laisse un trou d'octets nuls, qui tronque toute relecture en C.
    Le défaut existe encore dans l'`onLineRemoved` de `mp4track.cpp`.
  - `SaveTextInComment` est **sans effet en Matroska** : ce muxer écrit ses tags
    avec l'en-tête, or le texte des sous-titres n'est connu qu'à la fermeture.
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
- `libmedikit/supp_mp4v2.md` est un **brouillon périmé** de suppression totale de
  mp4v2. Deux écarts avec le code : sa « décision » de packetisation différée
  (option D, `FfRtpPacketizer`) a été **écartée** pour la lecture, qui conserve le
  contrat packetisé ci-dessus, et son contrat d'écriture (`AVFormatContext*`
  traversant l'API) l'a été aussi — `FfMediaFileWriter` ouvre le fichier
  lui-même, comme le lecteur. Ne pas le lire comme l'état du code.

### Texte et divers

- Texte : `medkit/text.h` (`UTF8Parser`, `TextFrame`),
  `medkit/text2subtitle.h` (accumulation RTT ↔ sous-titres, `SubtitleToRtt`),
  `medkit/red.h` (`RTPRedundantPayload`/`RTPRedundantEncoder`, RFC 2198/4103).
- Outils bas niveau : `medkit/bitstream.h`, `medkit/tools.h`,
  `medkit/avcdescriptor.h` (`avcC`, `H264SeqParameterSet`),
  `medkit/h263packet.h`, `medkit/videorescaler.h` (`VideoRescaler`, graphe
  avfilter persistant, CPU `scale` / VAAPI `scale_vaapi`), `medkit/logo.h` +
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
