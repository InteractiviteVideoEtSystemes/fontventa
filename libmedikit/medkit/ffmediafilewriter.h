#ifndef _FFMEDIAFILEWRITER_H_
#define _FFMEDIAFILEWRITER_H_

#ifdef __cplusplus

#include <string>
#include <vector>
#include <deque>
#include <sys/time.h>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/text.h>
#include <medkit/text2subtitle.h>

struct AVFormatContext;

/**
 *  Écrit un flux média dans un fichier, via ffmpeg/libavformat.
 *
 *  Le CONTENEUR est choisi d'après l'extension du nom de fichier : `.mp4`,
 *  `.mov`, `.3gp` (famille ISOBMFF), `.mkv`, `.mka` (Matroska), `.webm`.
 *
 *  Cette classe vit EN PARALLÈLE de `mp4writer` (mp4v2), qui reste le chemin
 *  d'Asterisk et de tout appelant déclarant ses pistes en cours
 *  d'enregistrement. Trois différences de contrat, toutes imposées par
 *  libavformat, et c'est pourquoi l'une ne remplace pas l'autre :
 *
 *  1. **Toutes les pistes se déclarent avant la première trame.** libavformat
 *     fige l'en-tête du fichier dès la première écriture ; aucune piste ne peut
 *     naître ensuite. Il n'y a donc PAS d'auto-création de piste dans
 *     `ProcessFrame` : une trame dont la piste n'existe pas rend -3.
 *  2. **Le writer possède le fichier** : il l'ouvre au constructeur et écrit le
 *     trailer à `Close()` (appelé par le destructeur). Aucun handle ne traverse
 *     l'API, donc pas d'ordre de destruction à respecter côté appelant.
 *  3. **Le délai initial n'est plus comblé par du média synthétique** (silence,
 *     trames noires) : la première trame de chaque piste porte l'horodatage réel
 *     et c'est le conteneur qui exprime le trou — edit list en ISOBMFF, absence
 *     de bloc en Matroska. Ni prologue vidéo ni pré-roll de silence ici.
 *
 *  Le TEXTE, lui, garde le comportement de `mp4writer` : chaque frappe produit
 *  immédiatement un échantillon de sous-titre, poussé jusqu'au fichier, et
 *  chaque ligne terminée part aussitôt dans le fichier texte annexe. Rien n'est
 *  retenu en attendant la frappe suivante ou la fermeture.
 *
 *  ATTENTION, le muxer `mp4` de ffmpeg REFUSE les codecs télécom : PCMU, PCMA,
 *  SLIN, AMR, G722, GSM, H263. Les enregistrer sans transcodage demande `.mkv`
 *  (tous) ou `.mov` (tous sauf G722 ; ce muxer refuse en plus Opus et VP8).
 *  Demander à `IsCodecSupported()` AVANT d'ouvrir la piste : c'est la seule
 *  réponse honnête, et elle est disponible avant la première trame.
 *
 *  HORLOGES (contrat identique à `mp4writer`) : les horodatages des trames sont
 *  en **millisecondes** pour l'audio et le texte, sur l'**horloge vidéo 90 kHz**
 *  pour la vidéo.
 */
class FfMediaFileWriter : private Text2Subtitle::Listener
{
public:
    enum Container
    {
        ContainerUnknown = 0,
        ContainerMp4,           // muxer "mp4"      (.mp4)
        ContainerMov,           // muxer "mov"      (.mov)
        Container3gp,           // muxer "3gp"      (.3gp)
        ContainerMatroska,      // muxer "matroska" (.mkv, .mka)
        ContainerWebm           // muxer "webm"     (.webm)
    };

    enum
    {
        TrackAudio    = 0,
        TrackVideo    = 1,
        TrackVideoDoc = 2,
        TrackText     = 3,
        TrackCount    = 4
    };

    /**
     * Ouvre `filename` en écriture ; le conteneur suit son extension.
     * Vérifier `IsOpen()` ensuite.
     * @param ctxdata: donnée opaque rendue par GetCtxData (comme mp4writer)
     * @param waitVideo: ne rien enregistrer avant la première image clé
     */
    FfMediaFileWriter( void * ctxdata, const char * filename, bool waitVideo );
    virtual ~FfMediaFileWriter();

    bool IsOpen() const { return fmtctx != NULL; }
    Container GetContainer() const { return container; }
    const char * GetContainerName() const;

    /** Conteneur déduit d'un nom de fichier, sans rien ouvrir. */
    static Container GetContainerFor( const char * filename );

    /**
     * Ce conteneur porte-t-il ce codec ? Interrogeable avant d'ouvrir la piste
     * — et avant même d'ouvrir le fichier, dans la forme statique. Répond non
     * pour tout codec que le muxer refuserait : il n'y a pas de transcodage
     * ici, le choix du conteneur appartient à l'appelant.
     */
    static bool IsCodecSupported( Container c, AudioCodec::Type codec );
    static bool IsCodecSupported( Container c, VideoCodec::Type codec );
    static bool IsTextSupported( Container c );

    bool IsCodecSupported( AudioCodec::Type codec ) const { return IsCodecSupported( container, codec ); }
    bool IsCodecSupported( VideoCodec::Type codec ) const { return IsCodecSupported( container, codec ); }
    bool IsTextSupported() const { return IsTextSupported( container ); }

    /**
     * Déclare une piste audio. À appeler AVANT la première trame.
     * @return 1 piste créée, 0 piste déjà déclarée, -1 refus (codec non porté
     *         par le conteneur, fichier non ouvert, en-tête déjà écrit)
     **/
    int AddTrack( AudioCodec::Type codec, DWORD samplerate, const char * trackName );

    /**
     * Déclare une piste vidéo. Mêmes codes de retour.
     * @param secondary: seconde piste vidéo (partage de document)
     **/
    int AddTrack( VideoCodec::Type codec, DWORD width, DWORD height, DWORD bitrate,
                  const char * trackName, bool secondary = false );

    /**
     * Déclare une piste texte (sous-titres). Mêmes codes de retour.
     * @param textfile: descripteur du fichier texte annexe, -1 si aucun. Il
     *        reçoit une ligne par ligne sortie de l'écran et alimente le tag
     *        `comment` du fichier (cf. SaveTextInComment).
     **/
    int AddTrack( TextCodec::Type codec, const char * trackName, int textfile );

    /**
     * Annonce une piste que l'appelant ne peut pas encore déclarer : une source
     * RTP ne livre son codec, sa fréquence et ses dimensions qu'avec sa
     * première trame, or l'en-tête se ferme à la première écriture. L'en-tête
     * ATTEND donc cette déclaration, et les trames des autres pistes patientent
     * dans la file d'attente. L'attente cesse au bout de `maxWaitMs`, ou si la
     * file déborde : l'en-tête s'écrit alors sans la piste, plutôt que de perdre
     * les autres. `AddTrack` la lève, y compris quand elle refuse la piste.
     *
     * @param track: TrackAudio, TrackVideo, TrackVideoDoc ou TrackText
     **/
    void ExpectTrack( int track, DWORD maxWaitMs );

    /**
     * Traite une trame.
     *
     * @return 1 = trame enregistrée
     *         0 = trame vide, ou écartée (attente de la vidéo)
     *        -1 = codec de la trame différent de celui de la piste
     *        -2 = média de la trame différent de celui de la piste
     *        -3 = pas de piste ouverte pour ce média
     *        -4 = codec non supporté
     *        -5 = échec d'écriture (libavformat)
     *      -333 = image clé demandée (l'appelant devrait émettre un FIR)
     **/
    int ProcessFrame( const MediaFrame * f, bool secondary = false );

    void * GetCtxData() { return ctxdata; }

    void SetParticipantName( const char * name );

    /**
     * Décale le média de `delayMs` sur la ligne de temps du fichier : sert au
     * participant qui rejoint une conférence déjà commencée. S'applique aux
     * pistes qui n'ont pas encore écrit leur première trame.
     */
    void SetInitialDelay( unsigned long delayMs );

    /**
     * @return 1: la vidéo a démarré (ou on ne l'attendait pas) ;
     *         0: toujours en attente ; -1: pas de piste vidéo
     */
    int IsVideoStarted();

    void SetWaitForVideo( bool wait ) { waitVideo = wait ? 1 : 0; }

    /**
     * Recopie le texte des sous-titres dans le tag `comment`. Sans effet en
     * Matroska : ce muxer écrit ses tags avec l'en-tête, or ce texte n'est
     * connu qu'à la fermeture.
     */
    void SaveTextInComment( bool save ) { saveTxtInComment = save; }

    /**
     * Écrit un dernier échantillon de sous-titre, tenant le texte courant
     * jusqu'à maintenant. Les échantillons précédents sont déjà dans le fichier
     * (écriture au fil de l'eau) ; celui-ci ne sert qu'à ne pas laisser l'écran
     * se vider à la dernière frappe. Appelé par `Close()`.
     */
    void Flush();

    /**
     * Écrit le trailer et ferme le fichier. Idempotent, appelé par le
     * destructeur. @return 0 si tout est écrit, <0 sinon.
     */
    int Close();

    void DumpInfo();

private:
    struct Track;

    // Trame mise de côté tant que l'en-tête n'est pas écrit : les
    // horodatages y sont dans la base de temps de la PISTE, celle du flux
    // n'étant fixée par le muxer qu'à l'écriture de l'en-tête.
    struct Pending
    {
        int               track;
        std::vector<BYTE> data;
        QWORD             pts;
        QWORD             duration;
        bool              key;
    };

    // --- Text2Subtitle::Listener (alimente `textfile`)
    virtual void onNewLine( std::string & prevline );
    virtual void onLineRemoved( std::string & prevline );

    bool  OpenFile( const char * filename );
    Track * NewTrack( int idx );

    // L'en-tête n'est écrit qu'une fois toutes les pistes déclarables, ce qui
    // pour H264 suppose d'avoir vu SPS/PPS. @return true si l'en-tête est écrit.
    bool  MaybeWriteHeader();
    bool  DeclareStream( Track * tr );
    // Abandonne les pistes encore indéclarables -- déclarées sans paramètres de
    // codec, ou seulement annoncées par ExpectTrack -- pour ne pas perdre les
    // autres.
    void  GiveUpNotReadyTracks( const char * why );

    int   WriteSample( Track * tr, const BYTE * data, DWORD size,
                       QWORD pts, QWORD duration, bool key );
    // Remet un paquet au muxer, horodatages convertis. 0 = écrit, <0 = échec.
    int   WriteToMuxer( Track * tr, const BYTE * data, DWORD size,
                        QWORD pts, QWORD duration, bool key );
    void  FlushPending();

    int   ProcessAudio( const AudioFrame * f );
    int   ProcessVideo( const VideoFrame * f, bool secondary );
    int   ProcessText( const TextFrame * f );

    // Écrit le sous-titre à la position courante de la piste texte et avance
    // son horloge de `durMs`.
    void  WriteSubtitle( Track * tr, const std::string & utf8, QWORD durMs );
    // Vide la file d'entrelacement du muxer et le tampon AVIO.
    void  FlushToDisk();

    // Rend -333 (demande d'image clé) au plus une fois toutes les 2 s, 0 sinon.
    int   AskForIntra();

    // Récolte les paramètres du codec (SPS/PPS H264, sequence header AV1) dans
    // l'extradata de la piste. @return true quand la piste est déclarable.
    bool  HarvestVideoParams( Track * tr, const VideoFrame * f );

    // Origine de la piste sur la ligne de temps, dans sa base de temps.
    QWORD OriginFor( const Track * tr ) const;
    // Avance l'horloge de la piste de `deltaSrc` et rend le pts du sample.
    QWORD NextPts( Track * tr, QWORD deltaSrc );

    void * ctxdata;

    std::string       path;
    Container         container;
    AVFormatContext * fmtctx;
    Track *           tracks[TrackCount];

    bool headerWritten;
    bool headerFailed;
    bool closed;

    char           partName[80];
    unsigned long  initialDelay;
    int            waitVideo;
    bool           saveTxtInComment;
    struct timeval firstframets;
    struct timeval lastfur;         // dernière demande d'image clé

    std::deque<Pending> pending;
    DWORD               pendingBytes;

    // Texte : un seul accumulateur, la piste texte est unique.
    Text2Subtitle textEncoder;
    int           textFd;

    // Pistes annoncées par ExpectTrack, que l'en-tête attend, et depuis quand.
    bool           expected[TrackCount];
    DWORD          expectMs[TrackCount];
    struct timeval expectSince[TrackCount];
};

#endif /* __cplusplus */

#endif /* _FFMEDIAFILEWRITER_H_ */
