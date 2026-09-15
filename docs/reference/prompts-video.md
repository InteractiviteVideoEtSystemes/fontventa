# Prompts vidéo pour Asterisk

Ce document décrit les fichiers que produit `tools/IVES_convert.ksh` pour jouer
un prompt avec de la vidéo, et les contraintes que ces fichiers respectent.

## Pourquoi un prompt doit avoir de la vidéo

Un client WebRTC met le flux distant, audio et vidéo, dans un seul élément
`<video>`. Chrome n'en démarre la lecture qu'à la première image vidéo reçue.
Un prompt audio seul joué vers ce client reste donc muet : l'audio arrive, le
navigateur ne le restitue pas.

Un prompt destiné à un appel vidéo porte donc toujours une piste vidéo, même
fixe. Le script produit cette piste depuis une image de fond quand la source
n'a pas de vidéo.

## Deux formats de sortie

| Application Asterisk | Fichiers | Commande |
|---|---|---|
| `mp4play(prompt.mp4)` | un MP4 : H264 + ulaw + alaw (+ AMR), pistes hint | `IVES_convert.ksh -i source -o prompt.mp4 [-b image -r fps]` |
| `Playback(prompt)` | `prompt.h264` + `prompt.wav` | `IVES_convert.ksh -q -i source -o prompt.mp4 [-b image -r fps]` |

Le mode `-q` produit aussi le MP4. Sans audio dans la source, le `.wav` est du
silence de la durée de la vidéo.

`mp4play` est le chemin recommandé : il date les paquets vidéo au temps média et
préfixe SPS/PPS sur chaque intra. Voir la section « Piège » pour le chemin
`Playback`.

## Contraintes sur la piste H264

Le script encode, ou réencode, toute piste H264 pour respecter ces règles :

- **Profil Baseline** (`profile-level-id` 42xx1F). C'est le seul profil que
  négocient les clients WebRTC et les terminaux télécom. Une source en profil
  High ou Main est réencodée depuis l'original.
- **Une intra par seconde** (`-g` = fréquence d'images).
- **SPS et PPS devant chaque intra** (`repeat-headers`). Un récepteur qui perd
  le début du flux retrouve ses paramètres à l'intra suivante.
- **Tranches de 1200 octets au plus** (`slice-max-size`). Chaque tranche tient
  dans un paquet RTP sous le MTU.

Ces réglages sont dans `V_FFMPEG_OPTS_H264`. Ils s'appliquent aussi en mode
rapide (`-f`) et à la vidéo de fond.

Vérification d'un MP4 avec le lecteur réel de `mp4play` :

```sh
cd libmedikit && make ffmp4probe && ./ffmp4probe prompt.mp4
```

## Format du fichier `.h264` d'Asterisk

`format_h264` ne lit pas un flux H264 brut. Le fichier est une suite
d'enregistrements :

| Champ | Taille | Contenu |
|---|---|---|
| timestamp | 4 octets, réseau | durée de l'image en unités 90 kHz |
| longueur | 2 octets, réseau | taille de la charge utile ; bit 15 = marqueur RTP |
| charge utile | longueur | un paquet RTP H264, sans en-tête RTP |

`tools/mp4asterisk prompt.mp4` écrit `prompt.h264` depuis la piste hint H264 du
MP4. Il place SPS et PPS devant chaque intra, sans marqueur, et calcule la durée
exacte de chaque image.

## Piège : `Playback` et le timestamp RTP vidéo

`format_h264.c` (dépôt asteriskv) date chaque paquet avec `ast_tvnow()`.
`rtp.c` fait alors avancer le timestamp RTP vidéo avec l'horloge murale, paquet
par paquet. Une intra de plusieurs paquets part en rafale. Si la rafale
chevauche une milliseconde, ses paquets reçoivent deux timestamps différents
sans marqueur entre eux, et le récepteur jette l'image.

Le risque croît avec le nombre de paquets par intra. Une vidéo de fond de
petite taille en limite l'effet. Le correctif est dans `format_h264.c` :
dériver `delivery` du temps média du fichier, comme le fait `SetVideoDelivery`
dans `libmedikit/frameutils.cpp` pour `mp4play`.
