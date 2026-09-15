# TELCO-381 — Migration de fontventa vers ffmpeg 9

Plan de portage du dépôt `fontventa`, et d'abord de `libmedikit`, de ffmpeg 5
vers le ffmpeg 9.0.1 empaqueté par IVèS.

## 1. État des lieux

### 1.1 Ce qui est installé sur la machine de référence

| Élément | Constat |
|---|---|
| Paquets | `ffmpeg-libs-9.0.1-2.ives.el9`, `ffmpeg-devel-9.0.1-2.ives.el9` (dépôt `ives-externals-testing-x86_64`) |
| Licence du paquet | `GPL-3.0-or-later` ; `avcodec_license()` rend « GPL version 3 or later » |
| En-têtes | `/usr/include/libavcodec`, `/usr/include/libavformat`, … Le répertoire `/usr/include/ffmpeg` **n'existe pas**. |
| Bibliothèques | `/usr/lib64/libavcodec.so.63`, `libavformat.so.63`, `libavutil.so.61`, `libswscale.so.10`, `libswresample.so.7`, `libavfilter.so.12` |
| Binaire `ffmpeg` | absent (`ffmpeg-libs` et `ffmpeg-devel` seulement) |
| Concurrent | `ffmpeg-5.1.10-1.el9` reste disponible dans `rpmfusion-free-updates`, sous le **même nom de paquet** |

Le paquet `ffmpeg-devel` IVèS livre des fichiers `.pc`. `pkg-config --cflags
libavcodec` rend une chaîne vide (préfixe système).

### 1.2 Configuration du ffmpeg 9 IVèS

Lue par `avcodec_configuration()` :

```
--disable-autodetect --enable-gpl --enable-version3
--enable-libx264 --enable-libvpx --enable-libdav1d --enable-libsvtav1 --enable-libopus
--enable-libspeex --enable-libgsm --enable-libopencore-amrnb --enable-libopencore-amrwb
--enable-libvo-amrwbenc --enable-whisper --enable-vaapi --enable-zlib
--pkg-config-flags=--static
```

`--enable-version3` est requis par opencore-amr et vo-amrwbenc (licence
Apache 2.0, compatible GPL v3 seulement). Il fait passer les bibliothèques
ffmpeg en « GPL v3 ou ultérieure ». Tout ce qui s'y lie doit accepter la GPL v3.
`fontventa.spec` dit « GPL » sans version ; Asterisk 1.4 est GPL v2. Le ffmpeg
5.1 de RPM Fusion a la même propriété : la migration ne change pas la
situation, elle la rend visible dans un paquet IVèS.

`--disable-autodetect` reste en place. Les bibliothèques de codecs ne sont
jamais auto-détectées : un `--enable-libxxx` explicite suffit et `configure`
échoue si le `-devel` manque. Le build est ainsi reproductible.

Les bibliothèques de codecs sont **liées en statique** dans `libavcodec`
(`ldd libavcodec.so.63` ne montre ni x264, ni vpx, ni opus, ni opencore, ni
speex, ni gsm). `ffmpeg-libs` n'ajoute donc aucune dépendance runtime pour
elles.

Codecs vus par libmedikit (sondés par `avcodec_find_encoder` /
`avcodec_find_decoder`, la primitive qu'utilise `IsCodecAvailable`) :

| Codec | Encodeur | Décodeur |
|---|---|---|
| H264 | `libx264`, `h264_vaapi` | `h264` |
| H263 / H263+ / MPEG4 | natif | natif |
| VP8 | `libvpx` | `vp8` |
| AV1 | `libsvtav1` | `libdav1d` |
| AAC | `aac` | `aac` |
| Opus | `libopus` | `opus` |
| PCMU / PCMA | natif | natif |
| G722 | `g722` | `g722` |
| Nellymoser | natif | natif |
| AMR-NB | `libopencore_amrnb` | `amrnb` |
| AMR-WB | `libvo_amrwbenc` | `amrwb` |
| GSM | `libgsm` | `gsm` |
| Speex | `libspeex` | `speex` |
| Périphériques matériels | `vaapi` | seul type déclaré |

Tous les codecs de libmedikit ont un encodeur et un décodeur.

### 1.3 Ce qui a été vérifié

Un build sonde a été fait sur une **copie** de `libmedikit` (hors dépôt), avec
les seules corrections de compilation listées en §2.1. Résultats sur le paquet
`9.0.1-2` :

| Vérification | Résultat |
|---|---|
| `make clean && make ASTERISK=no tests` | passe, **aucun** avertissement `deprecated` |
| `make ASTERISK=yes libmedkit.a` (en-têtes `asteriskv-devel` système) | passe |
| `./tests/runtests` | **182 / 182** |
| Aller-retour encodage puis décodage d'une seconde de signal 440 Hz par `AudioCodecFactory` | GSM, Speex 16 kHz, AMR-NB, AMR-WB, G722, Opus : 50 trames encodées, 50 trames décodées, nombre d'échantillons attendu |
| `make ffmp4probe` + lecture de `titi.mp4` | 1126 trames, 21 intra, seek ok, cadence 45,00 s pour 45,05 s de fichier |
| `make negotest` | se construit ; un contrôle échoue, sans lien avec ffmpeg (voir §2.6) |
| `make` à la racine : `app_mp4.so`, `app_rtsp.so`, `astlog`, `mp4creator` | passe ; `ldd -r app_mp4.so` ne laisse indéfinis que des symboles `ast_*` |
| `app_transcoder` | ne compile pas (API ffmpeg 0.x : `CODEC_ID_*`, `avcodec_open`, `me_method`). Il n'est pas dans la cible `all` et ne compilait pas non plus sur ffmpeg 5. |

Non vérifié : le test VAAPI `H264HwVaapi`, qui exige un GPU.

Le portage de l'API est **petit**. Le reste du travail porte sur les Makefiles,
le packaging et un durcissement.

## 2. Problèmes identifiés

### 2.1 Erreurs de compilation (bloquantes)

Quatre familles, toutes dans `libmedikit`. Ce sont les seules erreurs du build
brut.

| # | Fichier : ligne | Symbole disparu | Remplacement ffmpeg 9 |
|---|---|---|---|
| C1 | `ffvideocodec.h:164` (`IsKeyFrame`), `ffvideocodec.cpp:644` et `:650` | `AVFrame::key_frame` | `AVFrame::flags` et `AV_FRAME_FLAG_KEY` (`libavutil/frame.h`). Lecture : `(f->flags & AV_FRAME_FLAG_KEY) != 0`. Écriture : `flags |= …` / `flags &= ~…`. `pict_type` reste inchangé. |
| C2 | `h264/h264encoder.cpp:202`, `:205`, `:211` | `FF_PROFILE_H264_HIGH`, `_MAIN`, `_CONSTRAINED_BASELINE` | `AV_PROFILE_H264_*` (`libavcodec/defs.h`), mêmes valeurs. |
| C3 | `ffvideocodec.cpp:656-657` | `AV_INPUT_BUFFER_MIN_SIZE` | Supprimé sans successeur. Valait 16384. Garder un plancher local à la classe (constante nommée) ou retirer le plancher : le buffer sert de taille initiale de `VideoFrame`. Recommandation : constante locale de 16384, comportement identique. |
| C4 | `ffaudiocodec.cpp:101-118` (`IsSigned16FmtSupported`, `IsRateNativelySupported`) et `:151` (`TrySetRate`) | `AVCodec::sample_fmts`, `AVCodec::supported_samplerates` | `avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT | AV_CODEC_CONFIG_SAMPLE_RATE, 0, &ptr, &n)`. Contrat : retour `< 0` = erreur ; `ptr == NULL` = aucune contrainte ; sinon tableau de `n` éléments. Garder la sémantique actuelle « NULL = tout est accepté ». En `:151`, le premier format natif devient `ptr[0]` si `n > 0`. |

Une fois ces quatre points corrigés, gcc n'émet **aucun** avertissement de
dépréciation. Les symboles suivants, encore présents dans le code, existent
toujours en ffmpeg 9 sans dépréciation : `request_sample_fmt`, `FF_EC_*`,
`FF_COMPLIANCE_EXPERIMENTAL`, `FF_QP2LAMBDA`, `AV_CODEC_FLAG_QSCALE`,
`av_parser_*`, `sws_getContext`, `avcodec_get_hw_config`, l'API
`AVChannelLayout`.

`av_init_packet` et `avcodec_decode_video2` n'apparaissent que dans un bloc
**commenté** de `h264/h264decoder.cpp` (vers la ligne 505). Sans effet sur le
build ; à supprimer à l'occasion.

### 2.2 Asymétrie encodeur / décodeur dans `IsSupported` (latent)

`AudioCodec::IsSupported(Type)` (`codecs.cpp:59-75`) délègue au **décodeur**
seulement (`GSMDecoder::IsSupported()`, `AMRNBDecoder::IsSupported()`, …).
`GetSupportedCodecs` et le négociateur en dépendent.

Le paquet `9.0.1-1` avait les décodeurs natifs `amrnb`, `amrwb`, `gsm`, `speex`
mais aucun encodeur correspondant. `IsSupported` rendait alors **vrai**, le
négociateur acceptait ces codecs, et la création de l'encodeur échouait au
runtime avec `Encoder [amr_nb] not supported in ffmpeg`. Le paquet `9.0.1-2`
supprime le symptôme, pas la cause : la première recompilation de ffmpeg qui
oublie une bibliothèque le fera revenir sans qu'aucun test ne le voie.

### 2.3 Packaging et dépôts

| Sujet | Constat | Risque |
|---|---|---|
| `fontventa.spec` | `BuildRequires: ffmpeg-devel >= 5.0` | Satisfait par le 5.1.10 de RPM Fusion. Si l'image CI `rpmbuild_el9` n'active pas `ives-externals`, le build prend ffmpeg 5 et échoue sur §2.1 après ce portage. |
| `install.ksh prereq` | installe `ffmpeg-devel` sans version ; le commentaire cite RPM Fusion | Même risque ; commentaire faux. |
| `.gitlab-ci.yml` | pas de dépôt ajouté explicitement | À vérifier dans l'image `rpmbuild_el9` : le dépôt `ives-externals-testing` doit y être actif. Non vérifiable depuis cette machine. |
| Dépendances runtime auto-générées | `libavcodec.so.63`, `libavformat.so.63`, `libavutil.so.61`, `libswscale.so.10`, `libswresample.so.7` | Le RPM fontventa exigera `ffmpeg-libs` 9 IVèS sur les hôtes Asterisk. `dnf` remplacera le `ffmpeg-libs` 5.1.10 de RPM Fusion (même nom). Tout autre binaire de l'hôte lié à `.so.59` casse. À inventorier hôte par hôte (mediaserver / app_conference en premier). |
| `-lgsm` | encore lié par `app_mp4/Makefile` et par les exécutables de `libmedikit/Makefile` ; `gsm-devel` en `BuildRequires` | Plus aucun appel direct à libgsm dans le code (l'encodeur GSM passe par ffmpeg, qui embarque libgsm en statique). Vestige. Dépendance runtime `libgsm.so.1` inutile. |
| Cohabitation 5 / 9 | impossible : même nom de paquet | Une machine de build ne peut servir qu'une génération à la fois. |

### 2.4 Makefiles

| Sujet | Constat | Action |
|---|---|---|
| `libmedikit/Makefile`, repli `-I/usr/include/ffmpeg` | Le chemin n'existe pas avec le paquet IVèS. Le repli ne se déclenche que si `pkg-config` ignore ffmpeg. Inoffensif mais faux. | Garder le repli sur `pkg-config` seul ; remplacer le chemin en dur par une erreur explicite (`$(error …)`). |
| `FFMPEG_MODULES` sans `libavfilter` | `videorescaler.cpp` utilise libavfilter (`avfilter_graph_*`, `buffersrc`, `buffersink`). `libmedkit.a` contient donc des symboles `U avfilter_*` non couverts par `LDFLAGS` ni par `libmedkit.pc`. Ça lie aujourd'hui **par hasard** : seul `transcoder.o` (Asterisk) consomme `VideoRescaler`, et `app_mp4` ne le tire pas de l'archive. | Ajouter `libavfilter` à `FFMPEG_MODULES` (donc au `.pc`) et `-lavfilter` à `app_mp4/Makefile`. |
| `medkit/videorescaler.h` | inclut `<libavfilter/avfilter.h>` : dépendance **publique**, comme `avcodec.h` via `ffvideocodec.h` | Confirme le point précédent pour `Requires:` du `.pc`. |
| `app_mp4/Makefile` | `-lavcodec -lavformat -lavutil -lswscale -lswresample` en dur | Acceptable (préfixe système). Option : `pkg-config --libs`. Ajouter `-lavfilter` au minimum. |
| `app_transcoder/Makefile` | `-Wl,-Bstatic -lswscale -lavcodec -lavutil` : le paquet IVèS n'a pas de `.a` (`--disable-static`) | Module mort, hors cible `all`. Ne pas porter. Voir §5. |

### 2.5 Documentation périmée

- `CLAUDE.md` : « ffmpeg 5 », `-I/usr/include/ffmpeg`, `x264` en dépendance
  directe, `mp4v2` « attendu en statique sous `../../../staticdeps` » alors que
  `mp4v2-devel` système suffit ici.
- `libmedikit/g722/g722codec.cpp` : commentaire « ffmpeg 5 ».
- `install.ksh` : commentaire RPM Fusion.

### 2.6 Constaté en passant, sans lien avec ffmpeg

- `tools/negotest.cpp` attend pour H264 la chaîne
  `profile-level-id=42801f;packetization-mode=1`. Le négociateur produit
  désormais aussi `level-asymmetry-allowed=1`, et `tests/test_negotiator.cpp`
  l'attend. Le harnais est périmé ; le contrôle `video: H264 present avec sa
  chaine fmtp` échoue. À aligner sur le gtest, ou à supprimer si le gtest le
  remplace.
- Messages `Error splitting the input into NAL units` et `VP8 decoding error`
  pendant la suite : ils viennent des tests **adverses**, qui passent.
- `avcodec_get_supported_config` peut rendre `n == 0` avec `ptr != NULL` pour
  certains encodeurs. Traiter ce cas comme « aucune contrainte ».

## 3. Paquet ffmpeg

Le paquet `ffmpeg-9.0.1-2.ives` fournit tous les encodeurs et décodeurs que
libmedikit utilise (§1.2). Aucune modification de libmedikit n'est nécessaire
pour les codecs. Ce point est clos.

## 4. Plan de travail

Branche : `migration/ffmpeg9` (déjà créée, identique à `migration/almalinux_9`).
Un commit par étape. Chaque commit se construit et passe `make check`.

### Étape 0 — Prérequis hors dépôt

1. Inventorier les hôtes qui recevront `ffmpeg-libs` 9 et ce qui y est lié à
   `libavcodec.so.59`. Recommandation : livrer mediaserver et fontventa
   ensemble sur un hôte.
2. Vérifier que l'image CI `rpmbuild_el9` voit `ives-externals-testing`.

Critère : liste des hôtes et réponse sur l'image CI.

### Étape 1 — Portage de l'API (libmedikit)

Corriger C1 à C4 (§2.1). Supprimer le bloc commenté de `h264decoder.cpp`.
`make clean` d'abord : le Makefile suit les en-têtes via `-MMD`, mais le
changement d'en-têtes ffmpeg est externe.

Vérification : `make ASTERISK=no check` rend 182/182 ;
`make ffmp4probe && ./ffmp4probe tests/fixtures/titi.mp4` rend ~1126 trames et
un seek sur intra ; aucun avertissement `deprecated` dans la sortie.

### Étape 2 — Makefiles et `.pc`

1. `libmedikit/Makefile` : ajouter `libavfilter` à `FFMPEG_MODULES` ; remplacer
   le repli `-I/usr/include/ffmpeg` par un `$(error …)` qui nomme
   `PKG_CONFIG_PATH`.
2. `app_mp4/Makefile` : ajouter `-lavfilter` ; retirer `-lgsm`.
3. `libmedikit/Makefile` : retirer `-lgsm` des cibles `ffmp4probe`, `negotest`,
   `tests/runtests`.

Vérification : `nm -C libmedkit.a | grep " U avfilter"` liste des symboles et
`ldd app_mp4.so` montre `libavfilter.so.12` ; `check-undefined` passe ;
`ldd app_mp4.so | grep gsm` vide.

### Étape 3 — Durcissement de `IsSupported`

Faire porter `AudioCodec::IsSupported(Type)` sur **encodeur et décodeur** pour
les codecs adossés à ffmpeg : un codec est « supporté » si l'on sait le produire
et le consommer. Ajouter un test qui vérifie, pour chaque codec audio annoncé
par `GetSupportedCodecs`, que `CreateEncoder` et `CreateDecoder` rendent un
objet utilisable. C'est ce test qui aurait détecté le paquet `9.0.1-1`.

Ce point peut changer le résultat de la négociation pour les projets
consommateurs si un ffmpeg futur perd un encodeur. Le signaler dans la merge
request.

Vérification : `./tests/runtests --gtest_filter='Negotiator.*:AudioCodecs.*'`.

### Étape 4 — Packaging

1. `fontventa.spec` : `BuildRequires: ffmpeg-devel >= 9.0` ; retirer
   `gsm-devel` si l'étape 2 a retiré `-lgsm` ; entrée de changelog.
2. `install.ksh prereq` : même liste ; corriger le commentaire.
3. `.gitlab-ci.yml` : si l'étape 0 montre que l'image n'a pas le dépôt,
   l'ajouter dans le job `build` avant `./install.ksh prereq`.

Vérification : `./install.ksh rpm nosign` produit le RPM ;
`rpm -qp --requires fontventa-*.rpm | grep libav` montre `.so.63` / `.so.61`.

### Étape 5 — Documentation

- `CLAUDE.md` : ffmpeg 9, en-têtes via `pkg-config` (préfixe système), plus de
  `x264` direct, `mp4v2-devel` système suffisant, mention de l'asymétrie
  encodeur/décodeur et de la liste des codecs du paquet IVèS.
- Commentaire de `g722codec.cpp`.
- `tools/negotest.cpp` : aligner ou retirer (§2.6).
- Ce document : à supprimer une fois la migration livrée. Son contenu durable
  va dans `docs/reference/ffmpeg.md` (état courant : paquet, licence, codecs
  disponibles, contrat `IsSupported`).

### Étape 6 — Validation de bout en bout

1. `make clean && make ASTERISK=no check` : 182/182.
2. Test VAAPI sur un hôte à GPU :
   `./tests/runtests --gtest_also_run_disabled_tests --gtest_filter='*H264HwVaapi*'`.
3. `make clean && make` à la racine, puis charger `app_mp4.so` dans un
   Asterisk de test : `mp4play` sur `titi.mp4`, `mp4save` avec vidéo H264 et
   audio PCMU, puis relecture du fichier produit avec `ffmp4probe`.
4. Un enregistrement `.3gp` AMR réel par `mp4save`, relu par `ffmp4probe`.

### Étape 7 — Coordination avec les consommateurs

`app_conference` (mediaserver) embarque fontventa en sous-module et compile
`libmedkit.a` lui-même. Le `libmedikit/Makefile` référence déjà un plan
`ffmpeg9_migration_plan.md` côté mediaserver. Il faut :

- pointer le sous-module sur le commit final de `migration/ffmpeg9` ;
- livrer mediaserver et fontventa contre la même génération de `ffmpeg-libs`
  sur chaque hôte (§2.3) ;
- prévenir du changement de `IsSupported` (étape 3).

## 5. Hors périmètre

- `app_transcoder` : code sur l'API ffmpeg 0.x, hors cible `all`, non empaqueté.
  Le porter coûterait une réécriture complète pour un module que rien
  n'utilise. Recommandation : le retirer du dépôt dans une merge request
  séparée. Ce plan ne le touche pas.
- `mp4av`, `mp4creator`, `app_rtsp`, `astlog`, `tools` : aucune dépendance
  ffmpeg. Rien à faire.
- Suppression de mp4v2 (`libmedikit/supp_mp4v2.md`) : indépendante de ffmpeg 9.

## 6. Récapitulatif des risques

| Risque | Gravité | Parade |
|---|---|---|
| `IsSupported` positif sans encodeur si un ffmpeg futur perd une bibliothèque | moyenne | étape 3 |
| Build CI sur ffmpeg 5 par défaut | moyenne | spec `>= 9.0` ; dépôt dans l'image CI |
| Remplacement de `ffmpeg-libs` sur les hôtes | moyenne | inventaire (étape 0), livraison groupée |
| Licence GPL v3 des bibliothèques ffmpeg | à confirmer | même situation qu'avec RPM Fusion ; à noter dans le spec si la politique IVèS l'exige |
| `libavfilter` non lié | latente | étape 2 |
| Chemin `-I/usr/include/ffmpeg` faux | faible | étape 2 |
