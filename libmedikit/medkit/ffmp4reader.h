#ifndef _FFMP4READER_H_
#define _FFMP4READER_H_

#ifdef __cplusplus

#include <sys/time.h>
#include <vector>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/avcdescriptor.h>

struct AVFormatContext;
struct AVPacket;

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
    // Remplit `pending` avec le prochain paquet d'une piste sélectionnée.
    // @return false si EOF (pending laissé NULL).
    bool FillPending();
    // Échéance de lecture (cadencement) en ms, cadencée par dts et normalisée
    // sur le premier dts rencontré.
    long SchedMsOf( AVPacket * pkt );
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
    struct timeval  startPlaying;   // origine horloge murale (Rewind/Seek)
    long            schedOffsetMs;   // normalisation du 1er dts
    bool            schedOffsetSet;
    bool            eofReached;
    VideoFrame *    videoFrame;      // buffer réutilisable
    AudioFrame *    audioFrame;
    std::vector<BYTE> videoParamsAvcc; // SPS/PPS en AVCC (préfixe longueur)

    QWORD currentTs;
};

#endif /* __cplusplus */

#endif /* _FFMP4READER_H_ */
