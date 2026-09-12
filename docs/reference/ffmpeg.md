# ffmpeg dans fontventa

`libmedikit` s'appuie sur les bibliothèques ffmpeg pour ses codecs, la lecture
MP4 et le redimensionnement vidéo. Ce document décrit le paquet attendu, ce
qu'il fournit et les contrats de `libmedikit` qui en dépendent.

## Paquet

| Élément | Valeur |
|---|---|
| Paquets | `ffmpeg-libs`, `ffmpeg-devel` version 9, construits par IVèS |
| Dépôt | `ives-externals` (`https://rpm.ives.fr/9/externals-testing/x86_64/`) |
| Licence des bibliothèques | GPL version 3 ou ultérieure (`--enable-gpl --enable-version3`) |
| En-têtes | préfixe système : `/usr/include/libavcodec`, `/usr/include/libavformat`, … |
| Bibliothèques | `libavcodec.so.63`, `libavformat.so.63`, `libavutil.so.61`, `libswscale.so.10`, `libswresample.so.7`, `libavfilter.so.12` |
| Binaire `ffmpeg` | non fourni |

Le paquet `ffmpeg-devel` livre les fichiers `.pc`. `libmedikit/Makefile` ne
trouve ffmpeg **que par `pkg-config`** et s'arrête en erreur sinon. Pour un
ffmpeg hors préfixe système, renseigner `PKG_CONFIG_PATH`.

RPM Fusion publie un paquet `ffmpeg-libs` de version 5 sous le même nom. Une
machine ne porte qu'une des deux générations. Tout binaire lié à
`libavcodec.so.59` cesse de fonctionner quand la version 9 remplace la 5.

## Configuration du paquet

```
--disable-autodetect --enable-gpl --enable-version3
--enable-libx264 --enable-libvpx --enable-libdav1d --enable-libsvtav1 --enable-libopus
--enable-libspeex --enable-libgsm --enable-libopencore-amrnb --enable-libopencore-amrwb
--enable-libvo-amrwbenc --enable-vaapi --enable-zlib --pkg-config-flags=--static
```

- `--disable-autodetect` : rien n'entre dans le paquet par la seule présence
  d'un `-devel` sur la machine de build. Chaque bibliothèque est demandée
  explicitement et `configure` échoue si elle manque.
- `--enable-version3` : requis par opencore-amr et vo-amrwbenc (licence
  Apache 2.0, compatible GPL v3 seulement).
- `--pkg-config-flags=--static` : les bibliothèques de codecs sont liées en
  statique dans `libavcodec`. `ffmpeg-libs` n'ajoute aucune dépendance runtime
  pour elles, et `libmedikit` n'a rien à lier en plus.

## Codecs disponibles pour libmedikit

| Codec libmedikit | Encodeur ffmpeg | Décodeur ffmpeg |
|---|---|---|
| H264 | `libx264`, `h264_vaapi` | `h264` |
| H263-1996, H263-1998, MPEG4 | natif | natif |
| VP8 | `libvpx` | `vp8` |
| AV1 | `libsvtav1` | `libdav1d` |
| AAC | `aac` | `aac` |
| Opus | `libopus` | `opus` |
| PCMU, PCMA | natif | natif |
| G722 | `g722` | `g722` |
| AMR-NB | `libopencore_amrnb` | `amrnb` |
| AMR-WB | `libvo_amrwbenc` | `amrwb` |
| GSM | `libgsm` | `gsm` |
| Speex | `libspeex` | `speex` |
| Nellymoser | natif | natif |
| VP6 | aucun | `vp6f` |

Accélération matérielle : `vaapi` est le seul type de périphérique déclaré.

## Contrat de capacités

`AudioCodec::IsSupported` et `VideoCodec::IsSupported` répondent pour le
**décodeur** : « le serveur sait recevoir ce codec ». `IsEncodingSupported` et
`GetSupportedEncoderCodecs` répondent pour l'encodeur. Les deux sens ne
coïncident pas toujours : ffmpeg décode des codecs qu'il n'encode pas, et un
paquet construit sans libopencore-amr, libgsm ou libspeex garde les décodeurs
natifs.

Un consommateur qui doit **émettre** interroge `IsEncodingSupported`. Le test
`tests/test_codec_catalogue.cpp` vérifie, sur le ffmpeg installé, que chaque
codec audio reçu sait aussi être émis et que chaque encodeur audio annoncé
produit des trames. VP6 est la seule asymétrie acceptée, côté vidéo.

## Points d'API ffmpeg 9 utilisés

- Trame clé : `AVFrame::flags` et `AV_FRAME_FLAG_KEY`.
- Profils H264 : `AV_PROFILE_H264_*`.
- Formats et fréquences natifs d'un encodeur : `avcodec_get_supported_config`.
  Une liste absente signifie « aucune contrainte ».
- Disposition des canaux : `AVChannelLayout` (`ch_layout`).
- Redimensionnement : graphe `libavfilter` (`scale`, `scale_vaapi`) dans
  `videorescaler.cpp`. `libavfilter` est donc une dépendance publique de
  `libmedkit.pc`.
