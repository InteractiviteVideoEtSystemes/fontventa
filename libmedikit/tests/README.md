# Tests automatisés de libmedikit (GoogleTest)

Suite de tests unitaires et d'intégration pour `libmedikit`, bâtie sur
**GoogleTest** (paquet système). Voir `libmedikit_tests_plan.md` (racine du dépôt
mediaserver) pour la conception et l'historique.

## Lancer les tests

Depuis le répertoire `libmedikit/` (pas depuis `tests/`) :

```sh
make check        # compile puis exécute toute la suite
make tests        # compile seulement (produit tests/runtests)
./tests/runtests  # relancer sans recompiler
```

Filtrer une suite ou un test précis :

```sh
./tests/runtests --gtest_filter='Negotiator.*'
./tests/runtests --gtest_filter='Mp4ReadTiti.LectureVideoReelle'
```

### Prérequis

- **GoogleTest système** : `gtest`/`gtest_main` (`pkg-config gtest`). Sur
  AlmaLinux 9 : `dnf install gtest-devel`.
- La cible lie contre `libmedkit.a`, qu'elle (re)construit au besoin via un
  sous-make **`ASTERISK=no`** (les modules couplés Asterisk — `transcoder`,
  `mp4format`, `framebuffer` — sont hors périmètre). Toujours passer par
  `make check`, jamais par un rebuild `ASTERISK=yes`.

## Organisation

| Fichier | Suite(s) | Objet |
|---|---|---|
| `test_env.cpp` | `Smoke` | Environment gtest global : installe `SetLogFunctions` (sinon les `Log()`/`Debug()` de libmedkit segfaultent) + smoke test |
| `test_negotiator.cpp` | `Negotiator` | `CodecNegotiator::Negotiate` (contrat `fmtpByPt`) |
| `test_h264_sps.cpp` | `H264Sps` | Décodage `H264SeqParameterSet` |
| `test_avcdescriptor.cpp` | `AvcDescriptor` | avcC : round-trip `Serialize`/`Parse` |
| `test_utf8parser.cpp` | `Utf8Parser` | `UTF8Parser` (encodage/décodage UTF-8) |
| `test_text2subtitle.cpp` | `Text2Subtitle` | Accumulation RTT / sous-titres |
| `test_red.cpp` | `Red` | `RTPRedundantPayload::ParseRed` (RED, RFC 2198/4103) |
| `test_mp4_read.cpp` | `Mp4Read` | `Mp4FfReader` sur `record.mp4` : métadonnées + contrat AAC |
| `test_mp4_read_titi.cpp` | `Mp4ReadTiti` | `Mp4FfReader` sur `titi.mp4` : lecture vidéo réelle |
| `test_mp4_roundtrip.cpp` | `Mp4RoundTrip` | Écriture (`mp4writer`) → relecture (`Mp4FfReader`) audio PCMU + vidéo H264 |
| `test_mp4_transcode.cpp` | `Mp4Transcode` | Transcodage `titi.mp4` : H264→H263 + AAC→AMR-NB → enregistrement `.3gp` → relecture |

### Fixtures (`tests/fixtures/`, versionnées)

- **`record.mp4`** — AAC 48 kHz / H264 640×480 / mov_text, ~16.8 s. Sert aux
  **métadonnées** et au contrat AAC. ⚠️ Son flux H264 est défectueux (ffmpeg le
  rejette) : la lecture vidéo cadencée n'est donc pas testée sur ce fichier.
- **`titi.mp4`** — H264 640×360 / AAC 44.1 kHz stéréo, ~45 s. Flux H264 sain :
  sert au test de **lecture vidéo réelle**.

Chemins injectés à la compilation via `-DTEST_MP4_FILE` / `-DTEST_MP4_TITI_FILE`
(surchargeables : `make check TEST_MP4=/chemin TEST_MP4_TITI=/chemin`). Les tests
`GTEST_SKIP()` si la fixture est absente.

## Conventions

- Chaque module a ses tests **nominaux** et **adverses** (entrées malformées /
  hostiles). Les tests adverses ont, à plusieurs reprises, motivé un
  durcissement du code de production (`h264.h`, `red.cpp`) : refuser proprement
  une entrée invalide plutôt que lire hors limites.
- `gtest_main` fournit `main()` ; l'Environment global s'enregistre via un
  initialiseur statique dans `test_env.cpp`.
- **Piège d'inclusion** : `h264/h264.h` référence `Debug()`/`Log()` → inclure
  `medkit/log.h` **avant** `h264/h264.h`.

## Artefacts

`runtests`, `*.o` et `*.tmp.mp4` sont ignorés (`.gitignore`). Les fixtures
`fixtures/*.mp4` sont, elles, versionnées.
