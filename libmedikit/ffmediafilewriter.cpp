#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "medkit/log.h"
#include "medkit/tools.h"
#include "medkit/avcdescriptor.h"
#include "medkit/ffmediafilewriter.h"
#include "aac/aacconfig.h"
#include "av1/av1obu.h"
#include "h264/h264.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

// Durée maximale d'affichage d'un sous-titre, comme mp4writer.
#define MAX_SUBTITLE_DURATION 7000

// Bornes de la file d'attente précédant l'écriture de l'en-tête. Au-delà, on
// renonce aux pistes encore indéclarables plutôt que de gonfler indéfiniment.
#define MAX_PENDING_BYTES   ( 4 * 1024 * 1024 )
#define MAX_PENDING_SAMPLES 4000

struct FfMediaFileWriter::Track
{
    MediaFrame::Type media;
    int              codec;         // valeur de AudioCodec/VideoCodec/TextCodec
    enum AVCodecID   avcodec;
    AVStream *       stream;        // NULL avant l'écriture de l'en-tête
    AVRational       srcTb;         // base de temps des horodatages fournis
    QWORD            nextPts;       // horloge de la piste, en unités srcTb
    bool             hasFirst;
    DWORD            prevTs;
    QWORD            samples;
    unsigned long    delayMs;
    std::string      name;

    DWORD            rate;                  // audio
    DWORD            width, height, bitrate; // vidéo
    bool             started;               // première image clé vue
    bool             ready;                 // extradata connue, ou inutile
    std::vector<BYTE> extradata;
};

// ---------------------------------------------------------------------------
// Conteneurs et codecs qu'ils portent
// ---------------------------------------------------------------------------

FfMediaFileWriter::Container FfMediaFileWriter::GetContainerFor( const char * filename )
{
    if( filename == NULL ) return ContainerUnknown;

    const char * ext = strrchr( filename, '.' );
    if( ext == NULL ) return ContainerUnknown;
    ext++;

    if( strcasecmp( ext, "mp4" ) == 0 )  return ContainerMp4;
    if( strcasecmp( ext, "mov" ) == 0 )  return ContainerMov;
    if( strcasecmp( ext, "3gp" ) == 0 )  return Container3gp;
    if( strcasecmp( ext, "mkv" ) == 0 )  return ContainerMatroska;
    if( strcasecmp( ext, "mka" ) == 0 )  return ContainerMatroska;
    if( strcasecmp( ext, "webm" ) == 0 ) return ContainerWebm;

    return ContainerUnknown;
}

/*
 * Les tables ci-dessous disent ce que le MUXER accepte, pas ce que la norme du
 * conteneur autorise : le muxer `mp4` de ffmpeg refuse tous les codecs télécom
 * (PCMU, PCMA, AMR, G722, H263), là où `mov` et `matroska` les portent. Un
 * enregistrement sans transcodage se joue donc sur le choix de l'extension, et
 * c'est à l'appelant de le faire — d'où IsCodecSupported dans l'API publique.
 * Chaque couple est vérifié par tests/test_ffmediafilewriter.cpp.
 */
bool FfMediaFileWriter::IsCodecSupported( Container c, AudioCodec::Type codec )
{
    switch( c )
    {
        case ContainerMp4:
            return codec == AudioCodec::AAC || codec == AudioCodec::OPUS;

        case ContainerMov:
            // Opus en est exclu : le muxer mov le renvoie avec « opus only
            // supported in MP4 ».
            return codec == AudioCodec::PCMU || codec == AudioCodec::PCMA
                || codec == AudioCodec::SLIN || codec == AudioCodec::AMR
                || codec == AudioCodec::AAC;

        case Container3gp:
            return codec == AudioCodec::AMR || codec == AudioCodec::AAC;

        case ContainerMatroska:
            return codec == AudioCodec::PCMU || codec == AudioCodec::PCMA
                || codec == AudioCodec::SLIN || codec == AudioCodec::G722
                || codec == AudioCodec::AMR  || codec == AudioCodec::AAC
                || codec == AudioCodec::OPUS;

        case ContainerWebm:
            return codec == AudioCodec::OPUS;

        default:
            return false;
    }
}

bool FfMediaFileWriter::IsCodecSupported( Container c, VideoCodec::Type codec )
{
    switch( c )
    {
        case ContainerMp4:
            return codec == VideoCodec::H264 || codec == VideoCodec::MPEG4
                || codec == VideoCodec::AV1;

        case ContainerMov:
            // VP8 en est exclu : « VP8 muxing is currently not supported ».
            return codec == VideoCodec::H264 || codec == VideoCodec::MPEG4
                || codec == VideoCodec::H263_1996 || codec == VideoCodec::AV1;

        case ContainerMatroska:
            return codec == VideoCodec::H264 || codec == VideoCodec::MPEG4
                || codec == VideoCodec::H263_1996 || codec == VideoCodec::VP8
                || codec == VideoCodec::AV1;

        case Container3gp:
            return codec == VideoCodec::H264 || codec == VideoCodec::MPEG4
                || codec == VideoCodec::H263_1996;

        case ContainerWebm:
            return codec == VideoCodec::VP8 || codec == VideoCodec::AV1;

        default:
            return false;
    }
}

bool FfMediaFileWriter::IsTextSupported( Container c )
{
    // WebM n'accepte que du WebVTT, dont le format d'échantillon (cue) n'a rien
    // à voir avec celui des deux autres : non traité.
    return c == ContainerMp4 || c == ContainerMov || c == Container3gp
        || c == ContainerMatroska;
}

static enum AVCodecID AvCodecForAudio( AudioCodec::Type codec )
{
    switch( codec )
    {
        case AudioCodec::PCMU:  return AV_CODEC_ID_PCM_MULAW;
        case AudioCodec::PCMA:  return AV_CODEC_ID_PCM_ALAW;
        case AudioCodec::SLIN:  return AV_CODEC_ID_PCM_S16BE;
        case AudioCodec::G722:  return AV_CODEC_ID_ADPCM_G722;
        case AudioCodec::AMR:   return AV_CODEC_ID_AMR_NB;
        case AudioCodec::AMRWB: return AV_CODEC_ID_AMR_WB;
        case AudioCodec::AAC:   return AV_CODEC_ID_AAC;
        case AudioCodec::OPUS:  return AV_CODEC_ID_OPUS;
        case AudioCodec::GSM:   return AV_CODEC_ID_GSM;
        default:                return AV_CODEC_ID_NONE;
    }
}

// Nombre d'échantillons d'une trame codée, 0 pour les codecs par échantillon.
static int FrameSizeFor( AudioCodec::Type codec, DWORD rate )
{
    switch( codec )
    {
        case AudioCodec::AAC:   return 1024;
        case AudioCodec::AMR:   return 160;                 // 20 ms à 8 kHz
        case AudioCodec::AMRWB: return 320;                 // 20 ms à 16 kHz
        case AudioCodec::GSM:   return 160;
        case AudioCodec::OPUS:  return rate / 50;           // 20 ms
        case AudioCodec::G722:  return rate / 50;
        default:                return 0;
    }
}

static enum AVCodecID AvCodecForVideo( VideoCodec::Type codec )
{
    switch( codec )
    {
        case VideoCodec::H264:       return AV_CODEC_ID_H264;
        case VideoCodec::H263_1996:  return AV_CODEC_ID_H263;
        case VideoCodec::H263_1998:  return AV_CODEC_ID_H263P;
        case VideoCodec::MPEG4:      return AV_CODEC_ID_MPEG4;
        case VideoCodec::VP8:        return AV_CODEC_ID_VP8;
        case VideoCodec::AV1:        return AV_CODEC_ID_AV1;
        default:                     return AV_CODEC_ID_NONE;
    }
}

// L'échantillon AVCC porte-t-il une NALU de ce type ? Préfixes de longueur sur
// 4 octets : c'est le format que produisent l'encodeur H264 (b_annexb=0) comme
// le dépacketiseur.
static bool AvccHasNalType( const BYTE * data, DWORD size, BYTE type )
{
    DWORD off = 0;
    while( off + 4 < size )
    {
        DWORD n = get4( data, off );
        if( n == 0 || off + 4 + n > size ) return false;   // corrompu : ne rien déduire
        if( ( data[off + 4] & 0x1F ) == type ) return true;
        off += 4 + n;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------

FfMediaFileWriter::FfMediaFileWriter( void * ctxdata, const char * filename, bool waitVideo )
{
    this->ctxdata    = ctxdata;
    container        = ContainerUnknown;
    fmtctx           = NULL;
    headerWritten    = false;
    headerFailed     = false;
    closed           = false;
    initialDelay     = 0;
    this->waitVideo  = waitVideo ? 1 : 0;
    saveTxtInComment = true;
    pendingBytes     = 0;
    textFd           = -1;

    for( int i = 0; i < TrackCount; i++ ) tracks[i] = NULL;

    SetParticipantName( "participant" );
    textEncoder.SetListener( this );
    gettimeofday( &firstframets, NULL );
    gettimeofday( &lastfur, NULL );

    if( filename != NULL ) path = filename;

    OpenFile( filename );
}

FfMediaFileWriter::~FfMediaFileWriter()
{
    Close();

    for( int i = 0; i < TrackCount; i++ )
    {
        if( tracks[i] ) delete tracks[i];
    }

    if( fmtctx )
    {
        if( fmtctx->pb ) avio_closep( &fmtctx->pb );
        avformat_free_context( fmtctx );
        fmtctx = NULL;
    }
}

bool FfMediaFileWriter::OpenFile( const char * filename )
{
    if( filename == NULL || *filename == 0 )
        return Error( "FfMediaFileWriter: nom de fichier vide.\n" );

    container = GetContainerFor( filename );
    if( container == ContainerUnknown )
        return Error( "FfMediaFileWriter: extension de [%s] inconnue "
                      "(.mp4 .mov .3gp .mkv .mka .webm attendues).\n", filename );

    AVFormatContext * ctx = NULL;

    // Muxer déduit du nom de fichier par ffmpeg : c'est la même table
    // d'extensions que GetContainerFor, qui ne sert qu'à connaître la famille
    // (format d'échantillon des sous-titres, tables de codecs).
    int err = avformat_alloc_output_context2( &ctx, NULL, NULL, filename );
    if( err < 0 || ctx == NULL )
    {
        char buf[256];
        av_strerror( err, buf, sizeof( buf ) );
        return Error( "FfMediaFileWriter: aucun muxer pour [%s]: %s\n", filename, buf );
    }

    err = avio_open( &ctx->pb, filename, AVIO_FLAG_WRITE );
    if( err < 0 )
    {
        char buf[256];
        av_strerror( err, buf, sizeof( buf ) );
        Error( "FfMediaFileWriter: ouverture de [%s] impossible: %s\n", filename, buf );
        avformat_free_context( ctx );
        return false;
    }

    fmtctx = ctx;
    Log( "FfMediaFileWriter: [%s] ouvert, conteneur %s.\n", filename, GetContainerName() );
    return true;
}

const char * FfMediaFileWriter::GetContainerName() const
{
    switch( container )
    {
        case ContainerMp4:      return "mp4";
        case ContainerMov:      return "mov";
        case Container3gp:      return "3gp";
        case ContainerMatroska: return "matroska";
        case ContainerWebm:     return "webm";
        default:                return "unknown";
    }
}

void FfMediaFileWriter::SetParticipantName( const char * name )
{
    if( name == NULL ) return;
    strncpy( partName, name, sizeof( partName ) );
    partName[sizeof( partName ) - 1] = 0;
}

void FfMediaFileWriter::SetInitialDelay( unsigned long delayMs )
{
    initialDelay = delayMs;

    // Seules les pistes qui n'ont rien écrit peuvent encore être décalées :
    // l'origine d'une piste entamée est déjà dans le fichier.
    for( int i = 0; i < TrackCount; i++ )
    {
        if( tracks[i] && !tracks[i]->hasFirst ) tracks[i]->delayMs = delayMs;
    }
}

// ---------------------------------------------------------------------------
// Déclaration des pistes
// ---------------------------------------------------------------------------

FfMediaFileWriter::Track * FfMediaFileWriter::NewTrack( int idx )
{
    Track * tr = new Track;

    tr->media    = MediaFrame::Audio;
    tr->codec    = 0;
    tr->avcodec  = AV_CODEC_ID_NONE;
    tr->stream   = NULL;
    tr->srcTb.num = 1;
    tr->srcTb.den = 1000;
    tr->nextPts  = 0;
    tr->hasFirst = false;
    tr->prevTs   = 0;
    tr->samples  = 0;
    tr->delayMs  = initialDelay;
    tr->rate     = 0;
    tr->width    = 0;
    tr->height   = 0;
    tr->bitrate  = 0;
    tr->started  = false;
    tr->ready    = false;

    tracks[idx] = tr;
    return tr;
}

int FfMediaFileWriter::AddTrack( AudioCodec::Type codec, DWORD samplerate, const char * trackName )
{
    if( !IsOpen() ) return -1;
    if( tracks[TrackAudio] != NULL ) return 0;

    if( headerWritten )
    {
        Error( "FfMediaFileWriter: piste audio demandée après la première trame "
               "-- libavformat a déjà figé l'en-tête.\n" );
        return -1;
    }

    if( !IsCodecSupported( container, codec ) )
    {
        Error( "FfMediaFileWriter: le conteneur %s ne porte pas l'audio %s.\n",
               GetContainerName(), AudioCodec::GetNameFor( codec ) );
        return -1;
    }

    Track * tr = NewTrack( TrackAudio );

    tr->media   = MediaFrame::Audio;
    tr->codec   = (int)codec;
    tr->avcodec = AvCodecForAudio( codec );
    tr->name    = ( trackName != NULL ) ? trackName : "";
    tr->ready   = true;

    // Fréquences imposées par le codec, et non par l'horloge RTP que l'appelant
    // connaît : Opus n'existe qu'à 48 kHz dans un fichier, et le G.722
    // échantillonne à 16 kHz alors que son horloge RTP annonce 8 kHz.
    if( codec == AudioCodec::OPUS )      tr->rate = 48000;
    else if( codec == AudioCodec::G722 ) tr->rate = 16000;
    else                                 tr->rate = ( samplerate > 0 ) ? samplerate : 8000;
    tr->srcTb.num = 1;
    tr->srcTb.den = tr->rate;

    Log( "FfMediaFileWriter: piste audio [%s] %s %u Hz déclarée.\n",
         tr->name.c_str(), AudioCodec::GetNameFor( codec ), tr->rate );
    return 1;
}

int FfMediaFileWriter::AddTrack( VideoCodec::Type codec, DWORD width, DWORD height,
                                 DWORD bitrate, const char * trackName, bool secondary )
{
    if( !IsOpen() ) return -1;

    int idx = secondary ? TrackVideoDoc : TrackVideo;
    if( tracks[idx] != NULL ) return 0;

    if( headerWritten )
    {
        Error( "FfMediaFileWriter: piste vidéo demandée après la première trame "
               "-- libavformat a déjà figé l'en-tête.\n" );
        return -1;
    }

    if( !IsCodecSupported( container, codec ) )
    {
        Error( "FfMediaFileWriter: le conteneur %s ne porte pas la vidéo %s.\n",
               GetContainerName(), VideoCodec::GetNameFor( codec ) );

        // Ne pas laisser waitVideo attendre une vidéo qui ne démarrera jamais :
        // audio et texte seraient jetés et le fichier resterait vide.
        if( !secondary && waitVideo )
        {
            Log( "FfMediaFileWriter: waitVideo désarmé (audio et texte seuls).\n" );
            waitVideo = 0;
        }
        return -1;
    }

    Track * tr = NewTrack( idx );

    tr->media   = MediaFrame::Video;
    tr->codec   = (int)codec;
    tr->avcodec = AvCodecForVideo( codec );
    tr->name    = ( trackName != NULL ) ? trackName : "";
    tr->width   = width;
    tr->height  = height;
    tr->bitrate = bitrate;
    tr->srcTb.num = 1;
    tr->srcTb.den = 90000;

    // H264 et AV1 ne se déclarent qu'avec leur extradata (avcC, av1C), qui
    // n'existe qu'une fois les paramètres du flux vus : l'en-tête attendra.
    tr->ready   = ( codec != VideoCodec::H264 && codec != VideoCodec::AV1 );

    Log( "FfMediaFileWriter: piste vidéo [%s] %s %ux%u déclarée.\n",
         tr->name.c_str(), VideoCodec::GetNameFor( codec ),
         (unsigned)width, (unsigned)height );
    return 1;
}

int FfMediaFileWriter::AddTrack( TextCodec::Type codec, const char * trackName, int textfile )
{
    if( !IsOpen() ) return -1;
    if( tracks[TrackText] != NULL ) return 0;

    if( headerWritten )
    {
        Error( "FfMediaFileWriter: piste texte demandée après la première trame "
               "-- libavformat a déjà figé l'en-tête.\n" );
        return -1;
    }

    if( !IsTextSupported( container ) )
    {
        Error( "FfMediaFileWriter: le conteneur %s ne porte pas de piste texte.\n",
               GetContainerName() );
        return -1;
    }

    Track * tr = NewTrack( TrackText );

    tr->media   = MediaFrame::Text;
    tr->codec   = (int)codec;
    // tx3g en ISOBMFF, S_TEXT/UTF8 en Matroska : mov_text n'existe pas dans le
    // second, et les deux formats d'échantillon diffèrent (cf. WriteSample).
    tr->avcodec = ( container == ContainerMatroska ) ? AV_CODEC_ID_SUBRIP
                                                     : AV_CODEC_ID_MOV_TEXT;
    tr->name    = ( trackName != NULL ) ? trackName : "";
    tr->ready   = true;
    tr->srcTb.num = 1;
    tr->srcTb.den = 1000;

    textFd = textfile;

    // Le fichier annexe appartient à l'appelant et peut avoir déjà servi : ce
    // qu'il contient n'est pas de cet enregistrement. Le vider ICI, avant la
    // première ligne écrite -- et remettre la position d'écriture au début, que
    // ftruncate ne déplace pas, sinon la première ligne atterrit au-delà de la
    // fin et laisse un trou d'octets nuls (qui tronque toute relecture en C).
    if( textFd >= 0 )
    {
        if( ::ftruncate( textFd, 0 ) == 0 ) ::lseek( textFd, 0, SEEK_SET );
    }

    // Étiquette du participant, comme mp4writer : elle n'est pas écrite comme
    // un échantillon à elle seule, elle entre dans l'accumulateur et ressort
    // donc dans le premier sous-titre réellement écrit.
    if( !tr->name.empty() )
    {
        char intro[200];
        snprintf( intro, sizeof( intro ), "[%s]\n", tr->name.c_str() );

        // Passer par TextFrame plutôt que d'élargir les octets un par un : le
        // nom peut être de l'UTF-8, que seul le parseur de TextFrame décode.
        TextFrame tf( 0, (const BYTE *)intro, strlen( intro ) );
        textEncoder.Accumulate( tf.GetWString() );
    }

    Log( "FfMediaFileWriter: piste texte [%s] %s déclarée.\n",
         tr->name.c_str(), TextCodec::GetNameFor( codec ) );
    return 1;
}

// ---------------------------------------------------------------------------
// En-tête : déclaration des flux libavformat
// ---------------------------------------------------------------------------

static void SetExtradata( AVCodecParameters * par, const std::vector<BYTE> & src )
{
    if( src.empty() ) return;

    par->extradata = (uint8_t *)av_mallocz( src.size() + AV_INPUT_BUFFER_PADDING_SIZE );
    if( par->extradata == NULL ) return;

    memcpy( par->extradata, &src[0], src.size() );
    par->extradata_size = src.size();
}

// OpusHead (RFC 7845 §5.1) : les deux muxers l'exigent en CodecPrivate. On ne
// dispose pas de l'en-tête d'origine du pair (le flux vient de RTP), on le
// reconstruit donc : mono, sans pré-saut ni gain.
static void BuildOpusHead( std::vector<BYTE> & out, DWORD rate )
{
    out.clear();
    out.reserve( 19 );
    const char * magic = "OpusHead";
    for( int i = 0; i < 8; i++ ) out.push_back( (BYTE)magic[i] );
    out.push_back( 1 );                     // version
    out.push_back( 1 );                     // canaux
    out.push_back( 0 ); out.push_back( 0 ); // pré-saut (LE)
    out.push_back( (BYTE)( rate & 0xFF ) );
    out.push_back( (BYTE)( ( rate >> 8 ) & 0xFF ) );
    out.push_back( (BYTE)( ( rate >> 16 ) & 0xFF ) );
    out.push_back( (BYTE)( ( rate >> 24 ) & 0xFF ) );
    out.push_back( 0 ); out.push_back( 0 ); // gain (LE)
    out.push_back( 0 );                     // mapping family
}

bool FfMediaFileWriter::DeclareStream( Track * tr )
{
    AVStream * st = avformat_new_stream( fmtctx, NULL );
    if( st == NULL ) return false;

    AVCodecParameters * par = st->codecpar;

    par->codec_id = tr->avcodec;
    st->time_base = tr->srcTb;

    switch( tr->media )
    {
        case MediaFrame::Audio:
            par->codec_type  = AVMEDIA_TYPE_AUDIO;
            par->sample_rate = tr->rate;
            av_channel_layout_default( &par->ch_layout, 1 );

            switch( tr->codec )
            {
                case AudioCodec::PCMU:
                case AudioCodec::PCMA:
                    par->format               = AV_SAMPLE_FMT_U8;
                    par->bits_per_coded_sample = 8;
                    par->block_align          = 1;
                    par->bit_rate             = tr->rate * 8;
                    break;

                case AudioCodec::SLIN:
                    par->format               = AV_SAMPLE_FMT_S16;
                    par->bits_per_coded_sample = 16;
                    par->block_align          = 2;
                    par->bit_rate             = tr->rate * 16;
                    break;

                case AudioCodec::AAC:
                {
                    AACSpecificConfig cfg( tr->rate, 1 );
                    tr->extradata.assign( cfg.GetData(), cfg.GetData() + cfg.GetSize() );
                    par->profile = FF_PROFILE_AAC_LOW;
                    break;
                }

                case AudioCodec::OPUS:
                    BuildOpusHead( tr->extradata, tr->rate );
                    break;

                default:
                    break;
            }

            /* Le muxer ISOBMFF REFUSE une piste audio à trames dont il ne
             * connaît pas la taille : « fatal error, input is not a single
             * packet, implement a AVParser for it ». Il n'a aucun moyen de la
             * déduire d'un paquet déjà encodé, c'est donc à nous de la donner
             * (0 = codec par échantillon, le muxer se débrouille). */
            par->frame_size = FrameSizeFor( (AudioCodec::Type)tr->codec, tr->rate );
            break;

        case MediaFrame::Video:
            par->codec_type = AVMEDIA_TYPE_VIDEO;
            par->width      = tr->width;
            par->height     = tr->height;
            par->format     = AV_PIX_FMT_YUV420P;
            par->bit_rate   = tr->bitrate * 1000;
            st->avg_frame_rate.num = 0;
            st->avg_frame_rate.den = 1;
            break;

        case MediaFrame::Text:
            par->codec_type = AVMEDIA_TYPE_SUBTITLE;
            break;

        default:
            break;
    }

    SetExtradata( par, tr->extradata );

    if( !tr->name.empty() )
        av_dict_set( &st->metadata, "title", tr->name.c_str(), 0 );

    tr->stream = st;
    return true;
}

void FfMediaFileWriter::GiveUpNotReadyTracks( const char * why )
{
    for( int i = 0; i < TrackCount; i++ )
    {
        if( tracks[i] == NULL || tracks[i]->ready ) continue;

        Error( "FfMediaFileWriter: piste %d abandonnée (%s).\n", i, why );
        delete tracks[i];
        tracks[i] = NULL;
    }
}

bool FfMediaFileWriter::MaybeWriteHeader()
{
    if( headerWritten ) return true;
    if( !IsOpen() || headerFailed ) return false;

    for( int i = 0; i < TrackCount; i++ )
    {
        if( tracks[i] != NULL && !tracks[i]->ready ) return false;
    }

    for( int i = 0; i < TrackCount; i++ )
    {
        Track * tr = tracks[i];
        if( tr == NULL || tr->stream != NULL ) continue;

        if( !DeclareStream( tr ) )
        {
            Error( "FfMediaFileWriter: déclaration du flux %d impossible, piste abandonnée.\n", i );
            delete tr;
            tracks[i] = NULL;
        }
    }

    // Les tags doivent être posés MAINTENANT : le muxer Matroska les écrit avec
    // l'en-tête, tout ce qui arrive ensuite (le texte des sous-titres) est perdu.
    if( partName[0] ) av_dict_set( &fmtctx->metadata, "artist", partName, 0 );

    int err = avformat_write_header( fmtctx, NULL );
    if( err < 0 )
    {
        char buf[256];
        av_strerror( err, buf, sizeof( buf ) );
        Error( "FfMediaFileWriter: avformat_write_header a échoué: %s\n", buf );
        // Ne pas retenter à chaque trame : l'en-tête ne dépend plus de rien
        // qu'une trame de plus puisse apporter.
        headerFailed = true;
        return false;
    }

    headerWritten = true;
    Log( "FfMediaFileWriter: en-tête %s écrit (%u flux).\n",
         GetContainerName(), fmtctx->nb_streams );

    FlushPending();
    return true;
}

// ---------------------------------------------------------------------------
// Écriture
// ---------------------------------------------------------------------------

QWORD FfMediaFileWriter::OriginFor( const Track * tr ) const
{
    // Position de la première trame sur la ligne de temps : le décalage demandé
    // par l'appelant, plus le temps réellement écoulé depuis l'ouverture du
    // fichier. C'est ce second terme qui garde les pistes alignées quand l'une
    // démarre après l'autre (négociation vidéo, attente d'une image clé) ; le
    // trou ainsi laissé est exprimé par le conteneur, pas comblé par du média.
    struct timeval start = firstframets;
    QWORD elapsedMs = getDifTime( &start ) / 1000;
    QWORD originMs  = (QWORD)tr->delayMs + elapsedMs;
    AVRational ms;
    ms.num = 1;
    ms.den = 1000;
    return (QWORD)av_rescale_q( (int64_t)originMs, ms, tr->srcTb );
}

QWORD FfMediaFileWriter::NextPts( Track * tr, QWORD deltaSrc )
{
    if( !tr->hasFirst )
    {
        tr->hasFirst = true;
        tr->nextPts  = OriginFor( tr );
    }
    else
    {
        tr->nextPts += deltaSrc;
    }
    return tr->nextPts;
}

int FfMediaFileWriter::WriteSample( Track * tr, const BYTE * data, DWORD size,
                                    QWORD pts, QWORD duration, bool key )
{
    if( data == NULL || size == 0 ) return 0;

    int idx = -1;
    for( int i = 0; i < TrackCount; i++ ) if( tracks[i] == tr ) idx = i;
    if( idx < 0 ) return -3;

    if( !headerWritten )
    {
        if( pendingBytes + size <= MAX_PENDING_BYTES && pending.size() < MAX_PENDING_SAMPLES )
        {
            Pending p;
            p.track    = idx;
            p.data.assign( data, data + size );
            p.pts      = pts;
            p.duration = duration;
            p.key      = key;

            pendingBytes += size;
            pending.push_back( p );

            tr->samples++;
            MaybeWriteHeader();
            return 1;
        }

        // Une piste reste indéclarable (H264 dont aucune trame ne porte de
        // SPS/PPS, par exemple) : l'abandonner plutôt que perdre les autres.
        GiveUpNotReadyTracks( "aucun paramètre de codec après la file d'attente pleine" );

        if( !MaybeWriteHeader() ) return -5;

        // La piste courante a pu être celle qu'on vient d'abandonner.
        if( tracks[idx] != tr ) return -3;
    }

    if( WriteToMuxer( tr, data, size, pts, duration, key ) < 0 ) return -5;

    tr->samples++;
    return 1;
}

/*
 * Un paquet vers le muxer. Les horodatages arrivent dans la base de temps de la
 * PISTE et sont convertis ici : celle du flux n'est connue qu'après
 * avformat_write_header, et le muxer Matroska la réécrit (1/1000).
 */
int FfMediaFileWriter::WriteToMuxer( Track * tr, const BYTE * data, DWORD size,
                                     QWORD pts, QWORD duration, bool key )
{
    if( tr->stream == NULL ) return -1;

    AVPacket * pkt = av_packet_alloc();
    if( pkt == NULL ) return -1;

    pkt->stream_index = tr->stream->index;
    // av_interleaved_write_frame refcompte le paquet (donc recopie) avant de le
    // mettre en file : le tampon de l'appelant n'a pas à lui survivre.
    pkt->data         = (uint8_t *)data;
    pkt->size         = size;
    pkt->pts          = av_rescale_q( (int64_t)pts, tr->srcTb, tr->stream->time_base );
    pkt->dts          = pkt->pts;
    pkt->duration     = av_rescale_q( (int64_t)duration, tr->srcTb, tr->stream->time_base );
    if( key ) pkt->flags |= AV_PKT_FLAG_KEY;

    int err = av_interleaved_write_frame( fmtctx, pkt );
    av_packet_free( &pkt );

    if( err < 0 )
    {
        char buf[256];
        av_strerror( err, buf, sizeof( buf ) );
        Error( "FfMediaFileWriter: écriture d'un échantillon impossible: %s\n", buf );
        return -1;
    }
    return 0;
}

void FfMediaFileWriter::FlushPending()
{
    while( !pending.empty() )
    {
        Pending & p  = pending.front();
        Track *   tr = tracks[p.track];

        if( tr != NULL )
            WriteToMuxer( tr, &p.data[0], p.data.size(), p.pts, p.duration, p.key );

        pendingBytes -= p.data.size();
        pending.pop_front();
    }
}

// ---------------------------------------------------------------------------
// Traitement des trames
// ---------------------------------------------------------------------------

int FfMediaFileWriter::ProcessFrame( const MediaFrame * f, bool secondary )
{
    if( f == NULL ) return 0;
    if( !IsOpen() || closed ) return -5;

    switch( f->GetType() )
    {
        case MediaFrame::Audio:
            return ProcessAudio( (const AudioFrame *)f );

        case MediaFrame::Video:
            return ProcessVideo( (const VideoFrame *)f, secondary );

        case MediaFrame::Text:
            return ProcessText( (const TextFrame *)f );

        default:
            return 0;
    }
}

int FfMediaFileWriter::ProcessAudio( const AudioFrame * f )
{
    Track * tr = tracks[TrackAudio];
    if( tr == NULL ) return -3;

    AudioFrame * f2 = (AudioFrame *)f;
    if( (int)f2->GetCodec() != tr->codec )
    {
        Log( "FfMediaFileWriter: trame audio %s sur une piste %s.\n",
             AudioCodec::GetNameFor( f2->GetCodec() ),
             AudioCodec::GetNameFor( (AudioCodec::Type)tr->codec ) );
        return -1;
    }

    if( waitVideo ) return 0;
    if( f2->GetLength() == 0 ) return 0;

    // Une trame AAC-LC porte toujours 1024 échantillons : durée constante, que
    // l'arrondi des horodatages en millisecondes ferait dériver.
    QWORD delta;
    if( tr->codec == AudioCodec::AAC )
    {
        delta = 1024;
    }
    else
    {
        DWORD ts  = f2->GetTimeStamp();
        DWORD dms = 20;
        if( tr->hasFirst && ts > tr->prevTs )
        {
            dms = ts - tr->prevTs;
            if( dms > 200 ) dms = 20;    // horodatage incohérent
        }
        delta = (QWORD)dms * tr->rate / 1000;
    }

    QWORD pts = NextPts( tr, delta );
    tr->prevTs = f2->GetTimeStamp();

    return WriteSample( tr, f2->GetData(), f2->GetLength(), pts, delta, true );
}

/*
 * Rien à enregistrer de cette trame, et c'est une image clé qui débloquerait la
 * situation : -333 le dit à l'appelant, qui émet un FIR. Espacé de 2 s, sinon
 * chaque trame P d'un flux bloqué en réclamerait une.
 */
int FfMediaFileWriter::AskForIntra()
{
    if( getDifTime( &lastfur ) / 1000 < 2000 ) return 0;

    gettimeofday( &lastfur, NULL );
    Debug( "FfMediaFileWriter: toujours pas d'image clé, nouvelle demande.\n" );
    return -333;
}

int FfMediaFileWriter::ProcessVideo( const VideoFrame * f, bool secondary )
{
    int     idx = secondary ? TrackVideoDoc : TrackVideo;
    Track * tr  = tracks[idx];
    if( tr == NULL ) return -3;

    VideoFrame * f2 = (VideoFrame *)f;
    if( (int)f2->GetCodec() != tr->codec )
    {
        Log( "FfMediaFileWriter: trame vidéo %s sur une piste %s.\n",
             VideoCodec::GetNameFor( f2->GetCodec() ),
             VideoCodec::GetNameFor( (VideoCodec::Type)tr->codec ) );
        return -1;
    }

    // Rien avant la première image clé : le décodeur n'aurait pas de quoi
    // s'initialiser, et c'est d'elle que viennent SPS/PPS.
    if( !tr->started )
    {
        if( !f2->IsIntra() ) return AskForIntra();
        tr->started = true;
    }

    // Piste indéclarable faute de paramètres de codec : la trame ne sert à rien,
    // et une image clé en porterait.
    if( !tr->ready && !HarvestVideoParams( tr, f2 ) ) return AskForIntra();

    if( waitVideo > 0 )
    {
        if( !f2->IsIntra() ) return AskForIntra();

        waitVideo--;
        if( waitVideo > 0 )
        {
            Log( "FfMediaFileWriter: image clé écartée volontairement.\n" );
            return -333;
        }
        Log( "FfMediaFileWriter: la vidéo démarre.\n" );
    }

    QWORD delta = 50 * 90;              // 20 im/s par défaut
    if( tr->hasFirst )
    {
        DWORD ts = f2->GetTimeStamp();
        if( ts > tr->prevTs )
            delta = ts - tr->prevTs;
        else
            Log( "FfMediaFileWriter: horodatage vidéo incohérent (%u <= %u), "
                 "durée par défaut.\n", (unsigned)ts, (unsigned)tr->prevTs );
    }

    QWORD pts = NextPts( tr, delta );
    tr->prevTs = f2->GetTimeStamp();

    /* Drapeau image clé : pour H264, seule une trame portant un IDR (NALU 5) en
     * est une. Le drapeau intra du dépacketiseur est plus large (il couvre les
     * porteuses de SPS/PPS et les rafraîchissements intra de x264) : marquer clé
     * une trame P pousse un lecteur à s'y caler, et le décodeur s'y casse. */
    bool key = f2->IsIntra();
    if( key && tr->codec == VideoCodec::H264 )
        key = AvccHasNalType( f2->GetData(), f2->GetLength(), 0x05 );

    return WriteSample( tr, f2->GetData(), f2->GetLength(), pts, delta, key );
}

int FfMediaFileWriter::ProcessText( const TextFrame * f )
{
    Track * tr = tracks[TrackText];
    if( tr == NULL ) return -3;

    TextFrame * f2 = (TextFrame *)f;

    if( waitVideo ) return 0;
    if( f2->GetLength() == 0 ) return 0;

    /* Durée de l'échantillon = intervalle qui PRÉCÈDE la frappe, comme
     * mp4writer. Elle n'est pas exacte -- la durée réelle d'affichage n'est
     * connue qu'à la frappe suivante --, mais c'est ce qui permet d'écrire
     * l'échantillon TOUT DE SUITE plutôt que de retenir le sous-titre courant
     * jusqu'à la frappe suivante. Le texte doit être dans le fichier au fil de
     * l'eau : un enregistrement interrompu garde ce qui a été tapé, et une
     * conversation longue ne le garde pas en mémoire. */
    QWORD durMs;

    if( !tr->hasFirst )
    {
        tr->hasFirst = true;
        tr->nextPts  = OriginFor( tr );
        durMs        = 100;             // durée d'amorce, comme mp4writer
    }
    else
    {
        DWORD ts = f2->GetTimeStamp();
        // Deux frappes dans la même milliseconde : 1 ms au minimum, sinon deux
        // échantillons partagent leur pts et libavformat rejette le second.
        durMs = ( ts > tr->prevTs ) ? ( ts - tr->prevTs ) : 1;
    }
    tr->prevTs = f2->GetTimeStamp();

    textEncoder.Accumulate( f2->GetWString() );

    std::string subtitle;
    textEncoder.GetSubtitle( subtitle );

    WriteSubtitle( tr, subtitle, durMs );
    return 1;
}

/*
 * Écrit le sous-titre à la position courante de la piste, puis avance son
 * horloge de `durMs`. L'horloge est donc la somme des durées écrites, comme
 * celle d'une piste mp4v2 : les échantillons se suivent sans trou ni
 * chevauchement, dans les deux conteneurs.
 */
void FfMediaFileWriter::WriteSubtitle( Track * tr, const std::string & utf8, QWORD durMs )
{
    QWORD pts   = tr->nextPts;
    QWORD shown = ( durMs > MAX_SUBTITLE_DURATION ) ? MAX_SUBTITLE_DURATION : durMs;

    tr->nextPts = pts + durMs;

    if( tr->avcodec == AV_CODEC_ID_MOV_TEXT )
    {
        // tx3g : [longueur sur 2 octets][UTF-8]
        std::vector<BYTE> sample;
        sample.push_back( (BYTE)( utf8.length() >> 8 ) );
        sample.push_back( (BYTE)( utf8.length() & 0xFF ) );
        sample.insert( sample.end(), utf8.begin(), utf8.end() );

        WriteSample( tr, &sample[0], sample.size(), pts, shown, true );

        // La durée d'un échantillon tx3g se déduit de l'écart au suivant : sans
        // un échantillon vide, le texte resterait affiché jusqu'à la frappe
        // suivante, aussi lointaine soit-elle.
        if( durMs > shown )
        {
            BYTE empty[2] = { 0, 0 };
            WriteSample( tr, empty, sizeof( empty ), pts + shown, durMs - shown, true );
        }
    }
    else if( !utf8.empty() )
    {
        // S_TEXT/UTF8 : le texte nu, la durée portée par le bloc. Pas
        // d'échantillon vide -- un bloc de taille nulle n'existe pas, et
        // l'absence de bloc efface déjà le texte.
        WriteSample( tr, (const BYTE *)utf8.data(), utf8.length(), pts, shown, true );
    }

    FlushToDisk();
}

/*
 * Pousse jusqu'au fichier ce qui est déjà écrit. Sans cela, « écrire le
 * sous-titre tout de suite » s'arrêterait à l'un des TROIS tampons empilés
 * entre nous et le disque, et il faut les trois appels :
 *   1. la file d'entrelacement de libavformat, qui retient les paquets le temps
 *      que les autres pistes rattrapent ;
 *   2. les tampons propres au muxer -- Matroska garde son Cluster courant en
 *      mémoire jusqu'à sa limite de taille ou de durée. Seul un paquet NULL
 *      passé à av_write_frame le referme (AVFMT_ALLOW_FLUSH) ; les muxers qui
 *      ne l'annoncent pas, dont mov/mp4, ignorent l'appel ;
 *   3. le tampon AVIO.
 */
void FfMediaFileWriter::FlushToDisk()
{
    if( !headerWritten || fmtctx == NULL ) return;

    av_interleaved_write_frame( fmtctx, NULL );
    av_write_frame( fmtctx, NULL );
    if( fmtctx->pb ) avio_flush( fmtctx->pb );
}

bool FfMediaFileWriter::HarvestVideoParams( Track * tr, const VideoFrame * f )
{
    VideoFrame * f2 = (VideoFrame *)f;

    if( tr->codec == VideoCodec::H264 )
    {
        const BYTE * data = f2->GetData();
        DWORD        len  = f2->GetLength();
        const BYTE * sps = NULL, * pps = NULL;
        DWORD        spsLen = 0, ppsLen = 0;
        DWORD        off = 0;

        while( off + 4 < len )
        {
            DWORD n = get4( data, off );
            if( n == 0 || off + 4 + n > len ) break;

            BYTE type = data[off + 4] & 0x1F;
            if( type == 0x07 && sps == NULL ) { sps = data + off + 4; spsLen = n; }
            if( type == 0x08 && pps == NULL ) { pps = data + off + 4; ppsLen = n; }
            off += 4 + n;
        }

        if( sps == NULL || pps == NULL || spsLen < 4 ) return false;

        H264SeqParameterSet parsed;
        bool haveSps = false;
        try
        {
            haveSps = parsed.Decode( (BYTE *)sps + 1, spsLen - 1 );
        }
        catch( std::exception & e )
        {
            haveSps = false;
        }

        if( haveSps && parsed.GetWidth() > 0 && parsed.GetHeight() > 0 )
        {
            // Les dimensions du SPS font foi : le dépacketiseur ne renseigne pas
            // celles de la trame, et l'appelant a pu en déclarer d'autres.
            tr->width  = parsed.GetWidth();
            tr->height = parsed.GetHeight();
        }

        AVCDescriptor desc;
        desc.SetConfigurationVersion( 1 );
        desc.SetAVCProfileIndication( sps[1] );
        desc.SetProfileCompatibility( sps[2] );
        desc.SetAVCLevelIndication( sps[3] );
        // Champ lengthSizeMinusOne de l'avcC : préfixes de longueur sur 4 octets.
        // Le constructeur ne l'initialise pas.
        desc.SetNALUnitLength( 3 );
        desc.AddSequenceParameterSet( (BYTE *)sps, spsLen );
        desc.AddPictureParameterSet( (BYTE *)pps, ppsLen );

        tr->extradata.resize( desc.GetSize() );
        if( desc.Serialize( &tr->extradata[0], tr->extradata.size() ) == (DWORD)-1 )
        {
            tr->extradata.clear();
            return false;
        }

        Log( "FfMediaFileWriter: avcC construit (%ux%u, profil %02x niveau %02x).\n",
             (unsigned)tr->width, (unsigned)tr->height, sps[1], sps[3] );
        tr->ready = true;
        return true;
    }

    if( tr->codec == VideoCodec::AV1 )
    {
        std::vector<AV1ObuRef> obus;
        if( !AV1ParseObuStream( f2->GetData(), f2->GetLength(), obus ) ) return false;

        for( size_t i = 0; i < obus.size(); i++ )
        {
            if( obus[i].type != AV1_OBU_SEQUENCE_HEADER ) continue;

            // ffmpeg attend l'extradata AV1 sous forme de flux d'OBU : on recopie
            // le sequence header entier, en-tête et taille compris.
            const BYTE * start = f2->GetData() + obus[i].headerPos;
            DWORD        size  = obus[i].payloadPos + obus[i].payloadLen - obus[i].headerPos;

            tr->extradata.assign( start, start + size );
            Log( "FfMediaFileWriter: sequence header AV1 retenu (%u octets).\n", (unsigned)size );
            tr->ready = true;
            return true;
        }
        return false;
    }

    tr->ready = true;
    return true;
}

// ---------------------------------------------------------------------------
// Fermeture
// ---------------------------------------------------------------------------

void FfMediaFileWriter::Flush()
{
    Track * tr = tracks[TrackText];
    if( tr == NULL || !tr->hasFirst ) return;

    /* Chaque échantillon portant la durée de l'intervalle PRÉCÉDENT, la piste
     * s'arrête à la dernière frappe : sans ce dernier échantillon, le texte
     * disparaît de l'écran pour tout le reste de l'enregistrement. */
    struct timeval start = firstframets;
    QWORD nowMs = (QWORD)initialDelay + getDifTime( &start ) / 1000;

    if( nowMs < tr->nextPts + 100 ) return;   // rien à tenir (ou déjà tenu)

    std::string subtitle;
    textEncoder.GetSubtitle( subtitle );

    Log( "FfMediaFileWriter: dernier sous-titre tenu %llu ms.\n",
         (unsigned long long)( nowMs - tr->nextPts ) );

    WriteSubtitle( tr, subtitle, nowMs - tr->nextPts );
}

/*
 * Texte accumulé dans le fichier annexe, relu pour le tag `comment`. Le
 * descripteur appartient à l'appelant : on ne le ferme pas.
 */
static void ReadTextFile( int fd, std::string & text )
{
    if( fd < 0 ) return;

    if( ::lseek( fd, 0, SEEK_SET ) < 0 ) return;

    // "\r\n" en tête, pour l'extraction par IVES_convert.ksh et awk.
    text = "\r\n";

    char   buffer[256];
    ssize_t nRead = ::read( fd, buffer, sizeof( buffer ) );
    while( nRead > 0 )
    {
        text.append( buffer, nRead );
        nRead = ::read( fd, buffer, sizeof( buffer ) );
    }
}

int FfMediaFileWriter::Close()
{
    if( closed ) return 0;
    closed = true;

    if( fmtctx == NULL ) return -1;

    Flush();

    if( !headerWritten )
    {
        // Rien n'a encore été écrit : produire quand même un fichier valide,
        // sans les pistes restées indéclarables.
        GiveUpNotReadyTracks( "fichier fermé avant tout paramètre de codec" );

        int declared = 0;
        for( int i = 0; i < TrackCount; i++ ) if( tracks[i] ) declared++;

        if( declared == 0 )
        {
            // Aucune piste : il n'y a pas de fichier à produire, et libavformat
            // refuserait l'en-tête (« No streams to mux were specified »).
            Log( "FfMediaFileWriter: [%s] fermé sans aucune piste.\n", path.c_str() );
            avio_closep( &fmtctx->pb );
            return -1;
        }

        if( !MaybeWriteHeader() )
        {
            Error( "FfMediaFileWriter: [%s] fermé sans en-tête, fichier inutilisable.\n",
                   path.c_str() );
            avio_closep( &fmtctx->pb );
            return -1;
        }
    }

    if( saveTxtInComment && textFd >= 0 )
    {
        std::string texte;
        ReadTextFile( textFd, texte );

        if( texte.length() > 2 )
        {
            if( container == ContainerMatroska || container == ContainerWebm )
                Log( "FfMediaFileWriter: texte non recopié en tag -- Matroska écrit "
                     "ses tags avec l'en-tête, avant que ce texte n'existe.\n" );
            else
                av_dict_set( &fmtctx->metadata, "comment", texte.c_str(), 0 );
        }
    }

    int err = av_write_trailer( fmtctx );
    if( err < 0 )
    {
        char buf[256];
        av_strerror( err, buf, sizeof( buf ) );
        Error( "FfMediaFileWriter: av_write_trailer a échoué: %s\n", buf );
    }

    avio_closep( &fmtctx->pb );

    DumpInfo();
    return ( err < 0 ) ? -1 : 0;
}

int FfMediaFileWriter::IsVideoStarted()
{
    Track * tr = tracks[TrackVideo];
    if( tr == NULL ) return -1;
    if( waitVideo == 0 ) return 1;
    return tr->started ? 1 : 0;
}

void FfMediaFileWriter::DumpInfo()
{
    static const char * names[TrackCount] = { "Audio", "Video", "VideoDoc", "Text" };

    Log( "FfMediaFileWriter: [%s] conteneur %s.\n", path.c_str(), GetContainerName() );
    for( int i = 0; i < TrackCount; i++ )
    {
        if( tracks[i] == NULL ) continue;

        Log( "  piste %-8s flux %2d : %llu échantillons.\n", names[i],
             tracks[i]->stream ? tracks[i]->stream->index : -1,
             (unsigned long long)tracks[i]->samples );
    }
    Log( "-----------------\n" );
}

// ---------------------------------------------------------------------------
// Text2Subtitle::Listener — alimente le fichier texte annexe
// ---------------------------------------------------------------------------

void FfMediaFileWriter::onNewLine( std::string & prevline )
{
    if( textFd < 0 ) return;

    if( ::write( textFd, prevline.data(), prevline.length() ) < 0 )
        Error( "FfMediaFileWriter: écriture du fichier texte impossible.\n" );
}

/*
 * Ligne effacée à reculons (retours arrière au-delà du début de la ligne) : la
 * retirer aussi du fichier annexe, qui doit rester le texte réellement tapé.
 */
void FfMediaFileWriter::onLineRemoved( std::string & prevline )
{
    if( textFd < 0 || prevline.empty() ) return;

    off_t cur = ::lseek( textFd, 0, SEEK_CUR );
    if( cur < 0 ) return;

    off_t back = ( (off_t)prevline.size() < cur ) ? cur - (off_t)prevline.size() : 0;

    if( ::ftruncate( textFd, back ) < 0 ) return;
    // ftruncate ne déplace pas la position d'écriture : sans ce lseek, la frappe
    // suivante s'écrirait au-delà de la fin et laisserait un trou d'octets nuls.
    ::lseek( textFd, back, SEEK_SET );
}
