#include <string.h>
#include <string>
#include "medkit/log.h"
#include "medkit/tools.h"
#include "medkit/text.h"
#include "medkit/text2subtitle.h"
#include "medkit/red.h"
#include "medkit/ffmp4reader.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

// ---------------------------------------------------------------------------
// Mapping AVCodecID -> Codec::Type medkit
// ---------------------------------------------------------------------------
static bool MapVideoCodec( enum AVCodecID id, VideoCodec::Type & out )
{
    switch( id )
    {
        case AV_CODEC_ID_H264:  out = VideoCodec::H264;      return true;
        case AV_CODEC_ID_H263:  out = VideoCodec::H263_1996; return true;
        case AV_CODEC_ID_H263P: out = VideoCodec::H263_1998; return true;
        case AV_CODEC_ID_MPEG4: out = VideoCodec::MPEG4;     return true;
        case AV_CODEC_ID_VP8:   out = VideoCodec::VP8;       return true;
        default:                                             return false;
    }
}

static bool MapAudioCodec( enum AVCodecID id, AudioCodec::Type & out )
{
    switch( id )
    {
        case AV_CODEC_ID_PCM_MULAW: out = AudioCodec::PCMU; return true;
        case AV_CODEC_ID_PCM_ALAW:  out = AudioCodec::PCMA; return true;
        case AV_CODEC_ID_AMR_NB:    out = AudioCodec::AMR;  return true;
        case AV_CODEC_ID_AMR_WB:    out = AudioCodec::AMRWB;return true;
        case AV_CODEC_ID_OPUS:      out = AudioCodec::OPUS; return true;
        case AV_CODEC_ID_ADPCM_G722:out = AudioCodec::G722; return true;
        case AV_CODEC_ID_GSM:
        case AV_CODEC_ID_GSM_MS:    out = AudioCodec::GSM;  return true;
        // AAC volontairement non mappé en v1 (passthrough impossible vers un
        // pair télécom, cf. ffmpeg_mp4reader_plan.md décision c).
        default:                                            return false;
    }
}

// ---------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------
Mp4FfReader::Mp4FfReader( const char * filename )
{
    fmtctx           = NULL;
    videoStreamIdx   = -1;
    audioStreamIdx   = -1;
    textStreamIdx    = -1;
    videoCodec       = (VideoCodec::Type)-1;
    audioCodec       = (AudioCodec::Type)-1;
    videoNalLengthSize = 4;
    textEnabled      = false;
    textPtype        = 0;
    subConv          = NULL;
    redenc           = NULL;
    textFrame        = NULL;
    nextBOMorRepeat  = -1;
    pending          = NULL;
    schedOffsetMs    = 0;
    schedOffsetSet   = false;
    eofReached       = false;
    videoFrame       = NULL;
    audioFrame       = NULL;
    currentTs        = 0;
    setZeroTime( &startPlaying );

    Log( ">Mp4FfReader opening [%s]\n", filename );

    int err = avformat_open_input( &fmtctx, filename, NULL, NULL );
    if( err < 0 )
    {
        char buf[256];
        av_strerror( err, buf, sizeof(buf) );
        Error( "Mp4FfReader: avformat_open_input failed for [%s]: %s\n", filename, buf );
        fmtctx = NULL;
        return;
    }

    if( avformat_find_stream_info( fmtctx, NULL ) < 0 )
    {
        Error( "Mp4FfReader: avformat_find_stream_info failed for [%s]\n", filename );
        avformat_close_input( &fmtctx );
        fmtctx = NULL;
        return;
    }

    // Détection de la piste texte uniquement. Le choix des pistes audio/vidéo
    // est DIFFÉRÉ à OpenTrack : un enregistrement maison porte souvent la même
    // audio (et parfois la même vidéo) en plusieurs codecs ALTERNATIFS
    // (p.ex. PCMU ET PCMA sur toute la durée), et c'est l'appelant qui choisit,
    // via prefCodec, la piste correspondant au codec négocié avec le pair.
    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;

        if( par->codec_type == AVMEDIA_TYPE_SUBTITLE && textStreamIdx < 0 )
        {
            // Seul le mov_text (tx3g = 3GPP timed text) est exploitable : ses
            // échantillons sont [2 octets de longueur][UTF-8], convertibles en
            // T.140. Les autres formats de sous-titres sont ignorés.
            if( par->codec_id == AV_CODEC_ID_MOV_TEXT )
                textStreamIdx = i;
            else
                Log( "Mp4FfReader: subtitle stream %u codec %s non supporté (mov_text attendu)\n",
                     i, avcodec_get_name( par->codec_id ) );
        }
    }

    Log( "<Mp4FfReader opened [%s] (%u streams, sélection audio/vidéo à OpenTrack)\n",
         filename, fmtctx->nb_streams );
}

Mp4FfReader::~Mp4FfReader()
{
    if( pending ) av_packet_free( &pending );
    if( videoFrame ) delete videoFrame;
    if( audioFrame ) delete audioFrame;
    if( textFrame ) delete textFrame;
    if( subConv ) delete subConv;
    if( redenc ) delete redenc;
    if( fmtctx )
        avformat_close_input( &fmtctx );
}

// ---------------------------------------------------------------------------
// Sélection de pistes (compat API mp4reader) — v1 : valider le passthrough
// ---------------------------------------------------------------------------
int Mp4FfReader::OpenTrack( VideoCodec::Type outputCodecs[], unsigned int nbCodecs,
                            VideoCodec::Type prefCodec, bool cantranscode, bool secondary )
{
    // Parmi TOUTES les pistes vidéo mappables du fichier, choisir celle qui
    // correspond au codec préféré (prefCodec), sinon la mieux classée dans
    // outputCodecs (passthrough : le codec doit être demandé, pas de transcodage).
    int              bestIdx   = -1;
    VideoCodec::Type bestCodec = (VideoCodec::Type)-1;
    unsigned int     bestRank  = nbCodecs;   // rang dans outputCodecs

    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;
        if( par->codec_type != AVMEDIA_TYPE_VIDEO ) continue;
        VideoCodec::Type vc;
        if( !MapVideoCodec( par->codec_id, vc ) )
        {
            Log( "Mp4FfReader: video stream %u codec %s non supporté (v1)\n",
                 i, avcodec_get_name( par->codec_id ) );
            continue;
        }

        unsigned int rank = nbCodecs;
        for( unsigned int k = 0; k < nbCodecs; k++ )
            if( outputCodecs[k] == vc ) { rank = k; break; }
        if( rank == nbCodecs ) continue;   // codec non demandé

        if( vc == prefCodec ) { bestIdx = i; bestCodec = vc; break; }   // priorité absolue
        if( rank < bestRank ) { bestIdx = i; bestCodec = vc; bestRank = rank; }
    }

    if( bestIdx < 0 )
    {
        Log( "Mp4FfReader: aucune piste vidéo compatible (pas de transcodage v1)\n" );
        videoStreamIdx = -1;
        return 0;
    }

    videoStreamIdx = bestIdx;
    videoCodec     = bestCodec;

    // Taille du préfixe de longueur NALU (avcC) + buffers, propres à la piste choisie
    AVCodecParameters * par = fmtctx->streams[videoStreamIdx]->codecpar;
    videoNalLengthSize = 4;
    if( par->codec_id == AV_CODEC_ID_H264 &&
        par->extradata != NULL && par->extradata_size >= 5 )
        videoNalLengthSize = ( par->extradata[4] & 0x03 ) + 1;

    if( videoFrame ) { delete videoFrame; videoFrame = NULL; }
    videoFrame = new VideoFrame( videoCodec, 0x40000 /* 256KB, réalloue si besoin */ );
    videoFrame->SetH264NalSizeLength( videoNalLengthSize );
    if( videoCodec == VideoCodec::H264 )
        BuildVideoParams();

    Log( "Mp4FfReader: piste vidéo %d sélectionnée (%s)\n",
         videoStreamIdx, VideoCodec::GetNameFor( videoCodec ) );
    return 1;
}

int Mp4FfReader::OpenTrack( AudioCodec::Type outputCodecs[], unsigned int nbCodecs,
                            AudioCodec::Type prefCodec, bool cantranscode )
{
    // Parmi TOUTES les pistes audio mappables du fichier (souvent la même audio
    // en plusieurs codecs alternatifs — PCMU ET PCMA), choisir celle qui
    // correspond au codec préféré (prefCodec), sinon la mieux classée dans
    // outputCodecs. Passthrough : le codec doit être demandé, pas de transcodage.
    int              bestIdx   = -1;
    AudioCodec::Type bestCodec = (AudioCodec::Type)-1;
    unsigned int     bestRank  = nbCodecs;

    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;
        if( par->codec_type != AVMEDIA_TYPE_AUDIO ) continue;
        AudioCodec::Type ac;
        if( !MapAudioCodec( par->codec_id, ac ) )
        {
            Log( "Mp4FfReader: audio stream %u codec %s non supporté (v1)\n",
                 i, avcodec_get_name( par->codec_id ) );
            continue;
        }

        unsigned int rank = nbCodecs;
        for( unsigned int k = 0; k < nbCodecs; k++ )
            if( outputCodecs[k] == ac ) { rank = k; break; }
        if( rank == nbCodecs ) continue;   // codec non demandé

        if( ac == prefCodec ) { bestIdx = i; bestCodec = ac; break; }   // priorité absolue
        if( rank < bestRank ) { bestIdx = i; bestCodec = ac; bestRank = rank; }
    }

    if( bestIdx < 0 )
    {
        Log( "Mp4FfReader: aucune piste audio compatible (pas de transcodage v1)\n" );
        audioStreamIdx = -1;
        return 0;
    }

    audioStreamIdx = bestIdx;
    audioCodec     = bestCodec;

    if( audioFrame ) { delete audioFrame; audioFrame = NULL; }
    audioFrame = new AudioFrame( audioCodec, ClockRateFor( audioCodec ) );

    Log( "Mp4FfReader: piste audio %d sélectionnée (%s)\n",
         audioStreamIdx, AudioCodec::GetNameFor( audioCodec ) );
    return 1;
}

int Mp4FfReader::OpenTrack( TextCodec::Type c, BYTE pt, int rendering )
{
    // Pas de piste sous-titre mov_text dans ce fichier
    if( textStreamIdx < 0 )
    {
        Debug( "Mp4FfReader: aucune piste texte dans ce fichier\n" );
        return -1;
    }

    if( textEnabled )
    {
        Error( "Mp4FfReader: piste texte déjà ouverte\n" );
        return 0;
    }

    // Convertisseur sous-titre → RTT (T.140 incrémental) et, si T140RED, encodeur
    // de redondance RTP (même logique que l'ancien mp4reader mp4v2).
    subConv   = new SubtitleToRtt();
    textFrame = new TextFrame( true );
    textPtype = pt;
    if( c == TextCodec::T140RED )
        redenc = new RTPRedundantEncoder( pt );

    textEnabled     = true;
    nextBOMorRepeat = -1;   // (ré)armé après la première trame texte émise

    Log( "Mp4FfReader: piste texte %d ouverte (%s, pt=%u)\n",
         textStreamIdx, TextCodec::GetNameFor( c ), pt );
    return 1;
}

// ---------------------------------------------------------------------------
// Codecs
// ---------------------------------------------------------------------------
bool Mp4FfReader::GetVideoCodec( VideoCodec::Type & codec ) const
{
    if( videoStreamIdx < 0 ) return false;
    codec = videoCodec;
    return true;
}

bool Mp4FfReader::GetCodec( AudioCodec::Type & codec ) const
{
    if( audioStreamIdx < 0 ) return false;
    codec = audioCodec;
    return true;
}

bool Mp4FfReader::HasAudioCodec( AudioCodec::Type codec ) const
{
    if( !fmtctx ) return false;
    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;
        if( par->codec_type != AVMEDIA_TYPE_AUDIO ) continue;
        AudioCodec::Type ac;
        if( MapAudioCodec( par->codec_id, ac ) && ac == codec ) return true;
    }
    return false;
}

bool Mp4FfReader::HasVideoCodec( VideoCodec::Type codec ) const
{
    if( !fmtctx ) return false;
    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;
        if( par->codec_type != AVMEDIA_TYPE_VIDEO ) continue;
        VideoCodec::Type vc;
        if( MapVideoCodec( par->codec_id, vc ) && vc == codec ) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Métadonnées
// ---------------------------------------------------------------------------
double Mp4FfReader::GetDuration()
{
    if( !fmtctx || fmtctx->duration == AV_NOPTS_VALUE ) return 0;
    return fmtctx->duration / (double)AV_TIME_BASE;
}

DWORD Mp4FfReader::GetVideoWidth()
{
    if( videoStreamIdx < 0 ) return 0;
    return fmtctx->streams[videoStreamIdx]->codecpar->width;
}

DWORD Mp4FfReader::GetVideoHeight()
{
    if( videoStreamIdx < 0 ) return 0;
    return fmtctx->streams[videoStreamIdx]->codecpar->height;
}

DWORD Mp4FfReader::GetVideoBitrate()
{
    if( videoStreamIdx < 0 ) return 0;
    return (DWORD)fmtctx->streams[videoStreamIdx]->codecpar->bit_rate;
}

double Mp4FfReader::GetVideoFramerate()
{
    if( videoStreamIdx < 0 ) return 0.0;
    return av_q2d( fmtctx->streams[videoStreamIdx]->avg_frame_rate );
}

AVCDescriptor * Mp4FfReader::GetAVCDescriptor()
{
    if( videoStreamIdx < 0 || videoCodec != VideoCodec::H264 ) return NULL;

    AVCodecParameters * par = fmtctx->streams[videoStreamIdx]->codecpar;
    if( par->extradata == NULL || par->extradata_size <= 0 ) return NULL;

    // L'extradata H264 d'un MP4 EST l'AVCDecoderConfigurationRecord (avcC),
    // que AVCDescriptor::Parse sait décoder directement.
    AVCDescriptor * desc = new AVCDescriptor();
    if( !desc->Parse( par->extradata, par->extradata_size ) )
    {
        Error( "Mp4FfReader: échec du parsing de l'avcC (%d octets)\n", par->extradata_size );
        delete desc;
        return NULL;
    }
    return desc;
}

// ---------------------------------------------------------------------------
// Lecture cadencée (P2)
// ---------------------------------------------------------------------------
DWORD Mp4FfReader::ClockRateFor( AudioCodec::Type c )
{
    switch( c )
    {
        case AudioCodec::OPUS:  return 48000;
        case AudioCodec::AMRWB: return 16000;
        default:                return 8000; // PCMU/PCMA/GSM/AMR/G722
    }
}

void Mp4FfReader::BuildVideoParams()
{
    videoParamsAvcc.clear();
    AVCDescriptor * d = GetAVCDescriptor();
    if( !d ) { Error( "Mp4FfReader: pas d'avcC, préfixe SPS/PPS impossible\n" ); return; }

    // Concatène chaque SPS puis chaque PPS en AVCC : [len sur videoNalLengthSize][NALU]
    for( BYTE i = 0; i < d->GetNumOfSequenceParameterSets(); i++ )
    {
        BYTE * nalu = d->GetSequenceParameterSet( i );
        DWORD  sz   = d->GetSequenceParameterSetSize( i );
        for( int b = videoNalLengthSize - 1; b >= 0; b-- )
            videoParamsAvcc.push_back( (BYTE)( ( sz >> (8*b) ) & 0xFF ) );
        videoParamsAvcc.insert( videoParamsAvcc.end(), nalu, nalu + sz );
    }
    for( BYTE i = 0; i < d->GetNumOfPictureParameterSets(); i++ )
    {
        BYTE * nalu = d->GetPictureParameterSet( i );
        DWORD  sz   = d->GetPictureParameterSetSize( i );
        for( int b = videoNalLengthSize - 1; b >= 0; b-- )
            videoParamsAvcc.push_back( (BYTE)( ( sz >> (8*b) ) & 0xFF ) );
        videoParamsAvcc.insert( videoParamsAvcc.end(), nalu, nalu + sz );
    }
    delete d;
    Log( "Mp4FfReader: paramètres H264 (SPS/PPS) précalculés : %zu octets AVCC\n",
         videoParamsAvcc.size() );
}

bool Mp4FfReader::FillPending()
{
    if( pending ) return true;
    if( eofReached ) return false;

    if( !pending ) pending = av_packet_alloc();

    for(;;)
    {
        int r = av_read_frame( fmtctx, pending );
        if( r < 0 )
        {
            eofReached = true;
            av_packet_free( &pending );
            return false;
        }
        if( pending->stream_index == videoStreamIdx ||
            pending->stream_index == audioStreamIdx ||
            ( textEnabled && pending->stream_index == textStreamIdx ) )
            return true;
        // Piste ignorée (audio non supporté, texte non activé, data…) :
        // recycler et relire
        av_packet_unref( pending );
    }
}

long Mp4FfReader::SchedMsOf( AVPacket * pkt )
{
    AVStream * st = fmtctx->streams[pkt->stream_index];
    int64_t    s  = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
    int64_t    dts = ( pkt->dts == AV_NOPTS_VALUE ) ? pkt->pts : pkt->dts;
    long       ms  = (long)( ( dts - s ) * av_q2d( st->time_base ) * 1000.0 );

    if( !schedOffsetSet ) { schedOffsetMs = ms; schedOffsetSet = true; }
    return ms - schedOffsetMs;
}

MediaFrame * Mp4FfReader::BuildFrame( AVPacket * pkt )
{
    AVStream * st = fmtctx->streams[pkt->stream_index];
    double     tb = av_q2d( st->time_base );
    int64_t    s  = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
    int64_t    pts = ( pkt->pts == AV_NOPTS_VALUE ) ? pkt->dts : pkt->pts;

    if( pkt->stream_index == videoStreamIdx )
    {
        bool intra = ( pkt->flags & AV_PKT_FLAG_KEY ) != 0;

        // Trame intra : préfixer SPS/PPS (AVCC) devant les NALU du paquet (AVCC)
        if( intra && videoCodec == VideoCodec::H264 && !videoParamsAvcc.empty() )
        {
            videoFrame->SetMedia( &videoParamsAvcc[0], videoParamsAvcc.size() );
            videoFrame->AppendMedia( pkt->data, pkt->size );
        }
        else
        {
            videoFrame->SetMedia( pkt->data, pkt->size );
        }

        videoFrame->SetIntra( intra );
        videoFrame->SetTimestamp( (DWORD)( ( pts - s ) * tb * 90000.0 ) );
        videoFrame->ClearRTPPacketizationInfo();
        if( !videoFrame->Packetize( 1400 ) )
        {
            Error( "Mp4FfReader: échec packetisation vidéo (len=%u intra=%d)\n",
                   videoFrame->GetLength(), intra );
            return NULL;
        }
        return videoFrame;
    }
    else if( pkt->stream_index == audioStreamIdx )
    {
        DWORD rate = ClockRateFor( audioCodec );
        audioFrame->SetMedia( pkt->data, pkt->size );
        audioFrame->SetTimestamp( (DWORD)( ( pts - s ) * tb * rate ) );
        audioFrame->ClearRTPPacketizationInfo();
        if( !audioFrame->Packetize( 1400 ) )
        {
            Error( "Mp4FfReader: échec packetisation audio\n" );
            return NULL;
        }
        return audioFrame;
    }
    else // texte (mov_text / tx3g)
    {
        // Horloge T.140 = 1000 Hz → timestamp en millisecondes
        DWORD ts = (DWORD)( ( pts - s ) * tb * 1000.0 );

        // Échantillon tx3g : [2 octets big-endian = longueur][UTF-8][boîtes de style]
        unsigned int txtLen = 0;
        if( pkt->size >= 2 )
        {
            txtLen = ( (unsigned)pkt->data[0] << 8 ) | (unsigned)pkt->data[1];
            if( txtLen > (unsigned)pkt->size - 2 ) txtLen = pkt->size - 2; // garde-fou
        }

        textFrame->ClearRTPPacketizationInfo();

        if( txtLen > 0 )
        {
            // Différence avec le sous-titre précédent → T.140 temps réel :
            // caractères ajoutés + `nbdel` retours-arrière (0x08) en tête.
            std::string sub( (const char *)( pkt->data + 2 ), txtLen );
            std::string rtt;
            unsigned int nbdel = 0;
            subConv->GetTextDiff( sub, nbdel, rtt );
            if( nbdel > 0 ) rtt.insert( (size_t)0, (size_t)nbdel, (char)0x08 );

            textFrame->SetFrame( ts, (const BYTE *)rtt.data(), rtt.length() );
            if( nbdel > 0 )
                textFrame->AddRtpPacket( 0, nbdel, NULL, 0, true );
            textFrame->AddRtpPacket( nbdel, rtt.length() - nbdel, NULL, 0, true );
        }
        else
        {
            // Échantillon vide (effacement) : trame texte vide
            textFrame->SetFrame( ts, (const BYTE *)"", 0 );
        }

        // Enrobage RED (T140RED) si demandé, sinon T.140 nu
        if( redenc )
        {
            redenc->Encode( textFrame );
            MediaFrame * rf = redenc->GetRedundantPayload();
            if( rf ) rf->SetTimestamp( ts );
            return rf;
        }
        return textFrame;
    }
}

MediaFrame * Mp4FfReader::GetNextFrame( int & errcode, unsigned long & waittime )
{
    errcode  = 0;
    waittime = 0;

    if( !fmtctx ) { errcode = -1; return NULL; }

    // Assure un paquet en attente
    if( !FillPending() ) { errcode = -1; return NULL; } // EOF

    DWORD now = (DWORD)( getDifTime( &startPlaying ) / 1000 );
    long  t   = SchedMsOf( pending );

    // Pas encore l'heure : garder pending, demander à attendre
    if( (long)now < t )
    {
        // Phase idle du RTT : retransmettre la redondance (ou un BOM) pendant
        // qu'aucune trame texte n'est due — robustesse RFC 4103 (T140RED).
        if( redenc && nextBOMorRepeat >= 0 && (long)now >= nextBOMorRepeat )
        {
            if( redenc->IsIdle() ) redenc->EncodeBOM();
            else                   redenc->Encode( NULL );

            nextBOMorRepeat = redenc->IsIdle() ? (long)now + 5000 : (long)now + 100;
            errcode  = 1;
            waittime = 0;
            return redenc->GetRedundantPayload();
        }

        waittime = t - now;
        errcode  = 0;
        return NULL;
    }

    // Consommer pending
    bool wasText = ( pending->stream_index == textStreamIdx );
    currentTs = t;
    MediaFrame * f = BuildFrame( pending );
    av_packet_unref( pending );
    av_packet_free( &pending );

    if( f == NULL ) { errcode = -5; return NULL; }
    errcode = 1;

    // Après une trame texte, armer la retransmission RTT (utile en T140RED)
    if( wasText && redenc )
        nextBOMorRepeat = (long)now + 100;

    // Précharger le suivant pour calculer l'attente
    if( FillPending() )
    {
        long t2 = SchedMsOf( pending );
        waittime = ( (long)now >= t2 ) ? 0 : ( t2 - now );
    }
    else
    {
        waittime = 0; // EOF au prochain appel
    }

    return f;
}

int Mp4FfReader::Rewind()
{
    if( !fmtctx ) return 0;
    if( pending ) av_packet_free( &pending );
    av_seek_frame( fmtctx, -1, 0, AVSEEK_FLAG_BACKWARD );
    eofReached     = false;
    schedOffsetSet = false;
    schedOffsetMs  = 0;
    currentTs      = 0;
    if( subConv ) subConv->Reset();     // repartir d'un état RTT vierge
    nextBOMorRepeat = -1;
    gettimeofday( &startPlaying, 0 );  // (re)démarre l'horloge de lecture
    return 1;
}

bool Mp4FfReader::Eof()
{
    return eofReached && pending == NULL;
}

// Piste de référence pour le seek : la vidéo pilote (sync frames), sinon audio.
static int RefStreamIdx( int v, int a ) { return v >= 0 ? v : a; }

QWORD Mp4FfReader::Seek( QWORD timeMs )
{
    if( !fmtctx ) return timeMs;

    int refIdx = RefStreamIdx( videoStreamIdx, audioStreamIdx );
    if( refIdx < 0 ) return timeMs;

    AVStream * st = fmtctx->streams[refIdx];
    int64_t    s  = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
    int64_t    ts = av_rescale_q( (int64_t)timeMs, (AVRational){1,1000}, st->time_base ) + s;

    // Se cale sur la sync frame <= ts
    if( av_seek_frame( fmtctx, refIdx, ts, AVSEEK_FLAG_BACKWARD ) < 0 )
        Error( "Mp4FfReader: av_seek_frame(%lld ms) a échoué\n", (long long)timeMs );

    // Purge l'état de lecture puis recale l'horloge murale sur maintenant :
    // la timeline de cadencement redémarre à 0 au point de seek (schedOffset
    // sera pris sur le 1er paquet lu), donc pas d'attente initiale.
    if( pending ) av_packet_free( &pending );
    eofReached     = false;
    schedOffsetSet = false;
    schedOffsetMs  = 0;
    if( subConv ) subConv->Reset();
    nextBOMorRepeat = -1;
    gettimeofday( &startPlaying, 0 );

    // Peek de la 1re trame pour connaître le temps réel atteint (keyframe)
    QWORD actualMs = timeMs;
    if( FillPending() )
    {
        int64_t pts = ( pending->pts == AV_NOPTS_VALUE ) ? pending->dts : pending->pts;
        double  ms  = ( pts - s ) * av_q2d( st->time_base ) * 1000.0;
        actualMs = ( ms < 0 ) ? 0 : (QWORD)ms;   // clamp (dts de priming négatif)
    }
    currentTs = actualMs;
    return actualMs;
}

QWORD Mp4FfReader::PreSeek( QWORD timeMs )
{
    // Estime le temps de la sync frame <= timeMs SANS déplacer le curseur de
    // démux (via l'index du conteneur). Best-effort : renvoie timeMs si pas
    // d'index exploitable.
    if( !fmtctx ) return timeMs;

    int refIdx = RefStreamIdx( videoStreamIdx, audioStreamIdx );
    if( refIdx < 0 ) return timeMs;

    AVStream * st = fmtctx->streams[refIdx];
    int64_t    s  = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
    int64_t    ts = av_rescale_q( (int64_t)timeMs, (AVRational){1,1000}, st->time_base ) + s;

    int idx = av_index_search_timestamp( st, ts, AVSEEK_FLAG_BACKWARD );
    if( idx < 0 ) return timeMs;

    const AVIndexEntry * e = avformat_index_get_entry( st, idx );
    if( !e ) return timeMs;

    double ms = ( e->timestamp - s ) * av_q2d( st->time_base ) * 1000.0;
    return ( ms < 0 ) ? 0 : (QWORD)ms;   // clamp (dts de priming négatif)
}
