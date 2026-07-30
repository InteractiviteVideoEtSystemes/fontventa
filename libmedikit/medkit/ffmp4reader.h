#ifndef _FFMP4READER_H_
#define _FFMP4READER_H_

#ifdef __cplusplus

#include <sys/time.h>
#include <vector>
#include <deque>
#include <map>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/avcdescriptor.h>

struct AVFormatContext;
struct AVPacket;
struct SwrContext;

class TextFrame;
class SubtitleToRtt;
class RTPRedundantEncoder;

/**
 *  Lecteur MP4 basé sur ffmpeg/libavformat.
 *
 *  Contrairement au `mp4reader` historique (mp4v2, piloté par les hint tracks
 *  RTP), cette classe démuxe n'importe quel MP4 — hinté ou non — via
 *  libavformat, puis produit des `MediaFrame` medkit déjà packetisées RTP
 *  (contrat identique à `mp4reader::GetNextFrame`, cf. ffmpeg_mp4reader_plan.md).
 *
 *  v1 : passthrough natif (aucun transcodage). Vidéo H264/H263/VP8, audio
 *  télécom (PCMU/PCMA/AMR/G722/OPUS), texte mov_text→T.140 (T140/T140RED).
 *  AAC : hors v1 (transcodage requis vers un pair télécom).
 */
class Mp4FfReader
{
public:
    /** Ouvre le fichier via avformat_open_input. Vérifier IsOpen() ensuite. */
    Mp4FfReader( const char * filename );
    virtual ~Mp4FfReader();

    bool IsOpen() const { return fmtctx != NULL; }

    /**
     *  Sélectionne la piste audio parmi les codecs demandés (compat API
     *  mp4reader). En v1 : passthrough, `cantranscode` ignoré.
     *  @return >0 si une piste compatible est trouvée, 0 sinon.
     */
    int OpenTrack( AudioCodec::Type outputCodecs[], unsigned int nbCodecs,
                   AudioCodec::Type prefCodec, bool cantranscode );
    int OpenTrack( VideoCodec::Type outputCodecs[], unsigned int nbCodecs,
                   VideoCodec::Type prefCodec, bool cantranscode, bool secondary = false );
    int OpenTrack( TextCodec::Type c, BYTE pt, int rendering );

    /**
     *  Active le transcodage audio : lit la 1ʳᵉ piste audio décodable du fichier
     *  (AAC compris) et produit des trames dans `target` (codec télécom cible
     *  négocié avec le pair : PCMU/PCMA/…). Décodage → resampling → réencodage
     *  par tranches de 20 ms. @return 1 si activé, 0 sinon (aucune piste audio
     *  décodable, ou cible non encodable).
     */
    int OpenAudioTranscoded( AudioCodec::Type target );

    /** cf. mp4reader::GetNextFrame — implémenté en P2. */
    MediaFrame * GetNextFrame( int & errcode, unsigned long & waittime );

    int  Rewind();
    bool Eof();

    bool GetCodec( AudioCodec::Type & codec ) const;
    bool GetVideoCodec( VideoCodec::Type & codec ) const;

    // Interroge le fichier SANS effet de bord (ne change pas la piste
    // sélectionnée) : renvoie true si une piste mappable de ce codec existe.
    // Sert à la négociation de codec côté appelant (choix d'une alternative).
    bool HasAudioCodec( AudioCodec::Type codec ) const;
    bool HasVideoCodec( VideoCodec::Type codec ) const;

    // Métadonnées
    bool   HasAudioTrack()  { return audioStreamIdx >= 0; }
    bool   HasVideoTrack()  { return videoStreamIdx >= 0; }
    bool   HasTextTrack()   { return textStreamIdx  >= 0; }
    double GetDuration();
    DWORD  GetVideoWidth();
    DWORD  GetVideoHeight();
    DWORD  GetVideoBitrate();
    double GetVideoFramerate();
    AVCDescriptor * GetAVCDescriptor();

    // Seek / position (implémenté en P4)
    QWORD Seek( QWORD timeMs );
    QWORD PreSeek( QWORD timeMs );
    QWORD Tell()            { return currentTs; }

private:
    // Remplit `pending` avec le prochain paquet d'une piste sélectionnée, en
    // ordre dts croissant toutes pistes confondues (cf. readahead).
    // @return false si EOF (pending laissé NULL).
    bool FillPending();
    // Lit un paquet de plus dans les files de réordonnancement.
    // @return false si EOF atteint.
    bool ReadAhead();
    // Vide les files de réordonnancement (Rewind / Seek / destruction).
    void FlushReadAhead();
    // Jette les paquets en file antérieurs au plancher de seek.
    void DropBeforeSeekFloor();
    // Échéance de lecture (cadencement) en ms, cadencée par dts et normalisée
    // sur le premier dts rencontré.
    long SchedMsOf( AVPacket * pkt );
    // Idem sans normalisation ni effet de bord : utilisable pour comparer des
    // paquets avant de savoir lequel sera émis en premier.
    long RawSchedMsOf( AVPacket * pkt );
    // Construit la MediaFrame (réutilise videoFrame/audioFrame) packetisée RTP.
    MediaFrame * BuildFrame( AVPacket * pkt );
    // Précalcule SPS/PPS en AVCC pour préfixe des trames intra.
    void BuildVideoParams();
    DWORD ClockRateFor( AudioCodec::Type c );

    AVFormatContext * fmtctx;

    int videoStreamIdx;
    int audioStreamIdx;
    int textStreamIdx;      // -1 si absent ; piste mov_text (tx3g) détectée

    VideoCodec::Type videoCodec;
    AudioCodec::Type audioCodec;

    // Texte / sous-titres (T.140) — activé uniquement après OpenTrack(TextCodec).
    bool                  textEnabled;   // le demux ne remonte le texte que si vrai
    BYTE                  textPtype;     // payload type RTP (pour le red)
    SubtitleToRtt *       subConv;       // conversion sous-titre → T.140 incrémental
    RTPRedundantEncoder * redenc;        // enrobage RED si TextCodec::T140RED
    TextFrame *           textFrame;     // buffer réutilisable
    long                  nextBOMorRepeat; // ms, -1 = inactif (retransmission RTT idle)

    DWORD videoNalLengthSize;   // depuis avcC (1..4)

    // État de lecture (P2)
    AVPacket *      pending;        // paquet lu en avance, pas encore dû

    /* Réordonnancement des paquets par dts.
     *
     * mp4v2 interleave le fichier par tranches (~1 s de vidéo, puis ~1 s
     * d'audio), et av_read_frame restitue cet ordre de stockage, PAS l'ordre dts
     * global : chaque tranche arrive donc « déjà due » et serait émise en
     * rafale. Or Asterisk horodate les trames sortantes à l'instant d'émission ;
     * une rafale colle plusieurs unités d'accès sur un seul timestamp RTP et le
     * pair ne décode plus l'IDR. On lit donc en avance dans une file par piste
     * et on émet toujours la tête de plus petit dts. */
    std::map< int, std::deque<AVPacket *> > readahead;
    long            maxSchedRead;   // plus grand dts lu (ms), horizon de tri
    bool            maxSchedReadSet;
    /* Plancher posé par Seek : après un saut, le démux livre pour les pistes
     * éparses (texte) un échantillon antérieur au point atteint. Réordonné par
     * dts il serait émis en premier et fausserait la position. */
    long            seekFloorMs;
    bool            seekFloorSet;
    struct timeval  startPlaying;   // origine horloge murale (Rewind/Seek)
    long            schedOffsetMs;   // normalisation du 1er dts
    bool            schedOffsetSet;
    bool            eofReached;
    VideoFrame *    videoFrame;      // buffer réutilisable
    AudioFrame *    audioFrame;
    std::vector<BYTE> videoParamsAvcc; // SPS/PPS en AVCC (préfixe longueur)

    QWORD currentTs;

    // --- Transcodage audio (repli quand aucun codec du fichier n'est accepté
    //     par le pair, p.ex. fichier AAC vers un pair télécom). ---
    bool             audioTranscode;   // transcodage audio actif
    AudioCodec::Type audioSrcCodec;    // codec source (fichier) si transcodage
    AudioDecoder *   audioDec;         // décodeur source (AAC…)
    AudioEncoder *   audioEnc;         // encodeur cible (PCMU/PCMA…)
    SwrContext *     audioSwr;         // resampler srcRate -> encRate (S16 mono)
    std::vector<SWORD> pcmFifo;        // PCM S16 mono @ encRate en attente d'encodage
    DWORD            srcRate;          // fréquence de restitution du décodeur
    DWORD            encRate;          // fréquence d'entrée de l'encodeur cible
    DWORD            outFrameSamples;  // tranche d'encodage = encRate/50 (20 ms)
    QWORD            audioOutTs;        // horodatage RTP cible courant (échantillons)
    bool             audioOutTsSet;
    // Trame encodée prête à émettre (une par paquet RTP)
    struct EncFrame { std::vector<BYTE> data; DWORD ts; };
    std::deque<EncFrame> audioOutQueue;

    // Décode `pkt` (piste audio source), réencode par tranches de 20 ms vers
    // audioOutQueue. N'émet rien directement.
    void TranscodeAudioPacket( AVPacket * pkt );
    // Construit audioFrame à partir de audioOutQueue.front() et la dépile.
    MediaFrame * BuildTranscodedAudioFront();
    // Vide FIFO/file de sortie (Rewind/Seek).
    void ResetAudioTranscode();
};

#endif /* __cplusplus */

#endif /* _FFMP4READER_H_ */
