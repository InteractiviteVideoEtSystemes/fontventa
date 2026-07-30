#include <string.h>
#include <string>
#include "medkit/log.h"
#include "medkit/tools.h"
#include "medkit/text.h"
#include "medkit/text2subtitle.h"
#include "medkit/red.h"
#include "ffaudiocodec.h"
#include "ffvideocodec.h"
#include "medkit/ffmp4reader.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
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
    maxSchedRead     = 0;
    maxSchedReadSet  = false;
    seekFloorMs      = 0;
    seekFloorSet     = false;
    schedOffsetMs    = 0;
    schedOffsetSet   = false;
    eofReached       = false;
    videoFrame       = NULL;
    audioFrame       = NULL;
    currentTs        = 0;
    audioTranscode   = false;
    audioSrcCodec    = (AudioCodec::Type)-1;
    audioDec         = NULL;
    audioEnc         = NULL;
    audioSwr         = NULL;
    srcRate          = 0;
    encRate          = 0;
    outFrameSamples  = 0;
    audioOutTs       = 0;
    audioOutTsSet    = false;
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
    FlushReadAhead();
    if( videoFrame ) delete videoFrame;
    if( audioFrame ) delete audioFrame;
    if( audioDec ) delete audioDec;
    if( audioEnc ) delete audioEnc;
    if( audioSwr ) swr_free( &audioSwr );
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

int Mp4FfReader::OpenAudioTranscoded( AudioCodec::Type target )
{
    if( !fmtctx ) return 0;

    // 1) Trouver la 1ʳᵉ piste audio décodable (tout codec mappable, AAC compris).
    int              srcIdx  = -1;
    AudioCodec::Type srcType = (AudioCodec::Type)-1;
    for( unsigned i = 0; i < fmtctx->nb_streams; i++ )
    {
        AVCodecParameters * par = fmtctx->streams[i]->codecpar;
        if( par->codec_type != AVMEDIA_TYPE_AUDIO ) continue;

        AudioCodec::Type t;
        if( MapAudioCodec( par->codec_id, t ) )
            { srcIdx = i; srcType = t; break; }
    }
    if( srcIdx < 0 )
    {
        Log( "Mp4FfReader: aucune piste audio décodable pour transcodage\n" );
        return 0;
    }

    // 2) Décodeur source via la fabrique. L'extradata (AudioSpecificConfig pour
    //    l'AAC des MP4) est transmise génériquement : les codecs qui n'en ont
    //    pas besoin l'ignorent.
    AVCodecParameters * spar = fmtctx->streams[srcIdx]->codecpar;
    AudioDecoder * dec = AudioCodecFactory::CreateDecoder(
                             srcType, spar->extradata, (DWORD)spar->extradata_size );
    if( dec == NULL )
    {
        Error( "Mp4FfReader: décodeur source %s indisponible\n",
               AudioCodec::GetNameFor( srcType ) );
        return 0;
    }

    // 3) Encodeur cible. Les encodeurs télécom (PCMU/PCMA/G722…) NE resamplent
    //    PAS (TrySetRate renvoie leur fréquence native) : on rééchantillonne
    //    donc nous-mêmes source -> encRate via libswresample.
    AudioEncoder * enc = AudioCodecFactory::CreateEncoder( target );
    if( enc == NULL )
    {
        Error( "Mp4FfReader: encodeur cible %s indisponible\n",
               AudioCodec::GetNameFor( target ) );
        delete dec;
        return 0;
    }

    // Fréquence source : celle du conteneur (fiable dès l'ouverture), et non
    // dec->GetRate() qui peut valoir 0/8000 tant qu'aucune trame n'est décodée.
    int cpRate = fmtctx->streams[srcIdx]->codecpar->sample_rate;
    DWORD dr = ( cpRate > 0 ) ? (DWORD)cpRate : dec->GetRate();
    if( dr == 0 ) dr = 8000;          // sécurité
    DWORD er = enc->TrySetRate( dr ); // fréquence d'entrée réellement acceptée
    if( er == 0 ) er = 8000;

    // Resampler S16 mono dr -> er (si nécessaire).
    SwrContext * swr = NULL;
    if( dr != er )
    {
        AVChannelLayout mono;
        av_channel_layout_default( &mono, 1 );
        int e = swr_alloc_set_opts2( &swr,
                    &mono, AV_SAMPLE_FMT_S16, (int)er,
                    &mono, AV_SAMPLE_FMT_S16, (int)dr,
                    0, NULL );
        av_channel_layout_uninit( &mono );
        if( e < 0 || swr_init( swr ) < 0 )
        {
            Error( "Mp4FfReader: resampler %u->%u Hz KO\n", dr, er );
            if( swr ) swr_free( &swr );
            delete dec; delete enc;
            return 0;
        }
    }

    // Installation
    if( audioDec ) delete audioDec;
    if( audioEnc ) delete audioEnc;
    if( audioSwr ) swr_free( &audioSwr );
    if( audioFrame ) { delete audioFrame; audioFrame = NULL; }

    audioDec        = dec;
    audioEnc        = enc;
    audioSwr        = swr;
    audioStreamIdx  = srcIdx;
    audioSrcCodec   = srcType;
    audioCodec      = target;
    audioTranscode  = true;
    srcRate         = dr;
    encRate         = er;
    outFrameSamples = er / 50;         // 20 ms à la fréquence d'entrée encodeur (160@8k)
    if( outFrameSamples == 0 ) outFrameSamples = 160;
    audioOutTsSet   = false;
    pcmFifo.clear();
    audioOutQueue.clear();

    audioFrame = new AudioFrame( target, ClockRateFor( target ) );

    Log( "Mp4FfReader: transcodage audio %s(%u Hz) -> %s(%u Hz) (piste %d, tranche %u éch.)\n",
         AudioCodec::GetNameFor( srcType ), srcRate,
         AudioCodec::GetNameFor( target ), encRate, audioStreamIdx, outFrameSamples );
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

/* Horizon de réordonnancement : un paquet ne peut être émis qu'une fois qu'on a
 * lu au-delà de son dts de cette marge, sinon un paquet plus ancien pourrait
 * encore surgir d'une tranche suivante. Doit couvrir la taille de tranche de
 * l'écrivain (mp4v2 : ~1 s) ; 2 s ne coûtent qu'une poignée de centaines de
 * kio de tampon. */
#define READAHEAD_HORIZON_MS  2000

bool Mp4FfReader::ReadAhead()
{
    if( eofReached ) return false;

    AVPacket * pkt = av_packet_alloc();
    if( !pkt ) { eofReached = true; return false; }

    for(;;)
    {
        if( av_read_frame( fmtctx, pkt ) < 0 )
        {
            eofReached = true;
            av_packet_free( &pkt );
            return false;
        }

        if( pkt->stream_index == videoStreamIdx ||
            pkt->stream_index == audioStreamIdx ||
            ( textEnabled && pkt->stream_index == textStreamIdx ) )
        {
            // Antérieur au point de seek atteint : périmé, ne pas l'émettre.
            if( !seekFloorSet || RawSchedMsOf( pkt ) >= seekFloorMs ) break;
        }

        // Piste ignorée (audio non supporté, texte non activé, hint track…) ou
        // paquet périmé : recycler et relire
        av_packet_unref( pkt );
    }

    long ms = RawSchedMsOf( pkt );
    if( !maxSchedReadSet || ms > maxSchedRead )
    {
        maxSchedRead    = ms;
        maxSchedReadSet = true;
    }

    readahead[pkt->stream_index].push_back( pkt );
    return true;
}

void Mp4FfReader::FlushReadAhead()
{
    for( std::map< int, std::deque<AVPacket *> >::iterator it = readahead.begin();
         it != readahead.end(); it++ )
    {
        while( !it->second.empty() )
        {
            AVPacket * p = it->second.front();
            it->second.pop_front();
            av_packet_free( &p );
        }
    }
    readahead.clear();
    maxSchedRead    = 0;
    maxSchedReadSet = false;
}

void Mp4FfReader::DropBeforeSeekFloor()
{
    if( !seekFloorSet ) return;

    for( std::map< int, std::deque<AVPacket *> >::iterator it = readahead.begin();
         it != readahead.end(); it++ )
    {
        while( !it->second.empty() && RawSchedMsOf( it->second.front() ) < seekFloorMs )
        {
            AVPacket * p = it->second.front();
            it->second.pop_front();
            av_packet_free( &p );
        }
    }
}

bool Mp4FfReader::FillPending()
{
    if( pending ) return true;

    for(;;)
    {
        // Tête de file de plus petit dts, toutes pistes confondues.
        std::deque<AVPacket *> * best = NULL;
        long bestSched = 0;

        for( std::map< int, std::deque<AVPacket *> >::iterator it = readahead.begin();
             it != readahead.end(); it++ )
        {
            if( it->second.empty() ) continue;
            long s = RawSchedMsOf( it->second.front() );
            if( best == NULL || s < bestSched )
            {
                best      = &it->second;
                bestSched = s;
            }
        }

        // On ne peut l'émettre qu'une fois l'horizon dépassé : au-delà, aucun
        // paquet encore non lu ne peut avoir un dts plus ancien. En fin de
        // fichier plus rien ne viendra, on vide donc les files telles quelles.
        if( best != NULL && ( eofReached || maxSchedRead - bestSched >= READAHEAD_HORIZON_MS ) )
        {
            pending = best->front();
            best->pop_front();
            return true;
        }

        if( !ReadAhead() && best == NULL ) return false;   // EOF et rien en file
    }
}

long Mp4FfReader::RawSchedMsOf( AVPacket * pkt )
{
    AVStream * st = fmtctx->streams[pkt->stream_index];
    int64_t    s  = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
    int64_t    dts = ( pkt->dts == AV_NOPTS_VALUE ) ? pkt->pts : pkt->dts;

    return (long)( ( dts - s ) * av_q2d( st->time_base ) * 1000.0 );
}

long Mp4FfReader::SchedMsOf( AVPacket * pkt )
{
    long ms = RawSchedMsOf( pkt );

    if( !schedOffsetSet ) { schedOffsetMs = ms; schedOffsetSet = true; }
    return ms - schedOffsetMs;
}

void Mp4FfReader::TranscodeAudioPacket( AVPacket * pkt )
{
    // 1) Décodage source -> PCM S16 mono @ srcRate (le décodeur restitue par
    //    tranches ; on draine avec (NULL,0)).
    SWORD dpcm[8192];
    BYTE * in    = pkt->data;
    int    inLen = pkt->size;
    int    n;
    while( ( n = audioDec->Decode( in, inLen, dpcm, (int)(sizeof(dpcm)/sizeof(dpcm[0])) ) ) > 0 )
    {
        // 2) Rééchantillonnage srcRate -> encRate (ou copie directe si égal),
        //    puis empilage dans la FIFO à encRate.
        if( audioSwr )
        {
            // marge : ceil(n * er/dr) + latence swr
            int cap = (int)( (long long)n * encRate / ( srcRate ? srcRate : 1 ) ) + 64;
            std::vector<SWORD> tmp( cap );
            SWORD *   op[1]  = { &tmp[0] };
            const uint8_t * ip[1] = { (const uint8_t *)dpcm };
            int got = swr_convert( audioSwr, (uint8_t **)op, cap, ip, n );
            if( got > 0 ) pcmFifo.insert( pcmFifo.end(), tmp.begin(), tmp.begin() + got );
        }
        else
        {
            pcmFifo.insert( pcmFifo.end(), dpcm, dpcm + n );
        }
        in = NULL; inLen = 0;
    }

    // Base d'horodatage RTP cible calée sur le 1er paquet transcodé.
    if( !audioOutTsSet )
    {
        AVStream * st  = fmtctx->streams[audioStreamIdx];
        double     tb  = av_q2d( st->time_base );
        int64_t    s   = ( st->start_time == AV_NOPTS_VALUE ) ? 0 : st->start_time;
        int64_t    pts = ( pkt->pts == AV_NOPTS_VALUE ) ? pkt->dts : pkt->pts;
        if( pts == AV_NOPTS_VALUE ) pts = s;
        audioOutTs    = (QWORD)( ( pts - s ) * tb * ClockRateFor( audioCodec ) );
        audioOutTsSet = true;
    }

    // 3) Encodage par tranches de 20 ms (à encRate) -> 1 trame cible / tranche.
    const DWORD outInc = ClockRateFor( audioCodec ) / 50;   // 160@8k, 960@48k
    BYTE out[4096];
    while( pcmFifo.size() >= outFrameSamples )
    {
        int len = audioEnc->Encode( &pcmFifo[0], (int)outFrameSamples, out, (int)sizeof(out) );
        pcmFifo.erase( pcmFifo.begin(), pcmFifo.begin() + outFrameSamples );
        if( len <= 0 ) continue;

        EncFrame ef;
        ef.data.assign( out, out + len );
        ef.ts = (DWORD)audioOutTs;
        audioOutQueue.push_back( std::move( ef ) );
        audioOutTs += outInc;
    }
}

MediaFrame * Mp4FfReader::BuildTranscodedAudioFront()
{
    EncFrame & ef = audioOutQueue.front();
    audioFrame->SetMedia( ef.data.empty() ? (BYTE*)"" : &ef.data[0], ef.data.size() );
    audioFrame->SetTimestamp( ef.ts );
    audioFrame->ClearRTPPacketizationInfo();
    audioFrame->Packetize( 1400 );
    MediaFrame * r = audioFrame;
    audioOutQueue.pop_front();
    return r;
}

void Mp4FfReader::ResetAudioTranscode()
{
    // Recrée décodeur/encodeur pour purger leurs tampons internes (post-seek).
    if( audioTranscode ) OpenAudioTranscoded( audioCodec );
}

/*
 * L'échantillon AVCC contient-il une NALU du type demandé ?
 * `lenSize` = taille du préfixe de longueur (1..4, depuis l'avcC).
 */
static bool AvccHasNalType( const BYTE * data, DWORD size, DWORD lenSize, BYTE type )
{
    DWORD off = 0;
    while( off + lenSize < size )
    {
        DWORD n = 0;
        for( DWORD k = 0; k < lenSize; k++ ) n = ( n << 8 ) | data[off + k];
        if( n == 0 || off + lenSize + n > size ) return false;   // corrompu : ne rien déduire
        if( ( data[off + lenSize] & 0x1F ) == type ) return true;
        off += lenSize + n;
    }
    return false;
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

        /* Préfixer les SPS/PPS de l'avcC devant les NALU du paquet -- mais
         * SEULEMENT si l'échantillon porte un IDR sans ses propres paramètres.
         *
         * L'avcC ne mémorise que le PREMIER jeu SPS/PPS de la piste ; quand le
         * fichier en contient deux (prologue de trames noires encodé par nous,
         * puis flux réel du pair, tous deux en sps_id/pps_id 0), ce préfixe
         * injecte les paramètres du prologue AU MILIEU du flux réel. Constaté en
         * production : SPS prologue à log2_max_frame_num=4 contre 6 pour le flux
         * réel -- toute slice réelle parsée sous le SPS du prologue se décale de
         * 2 bits après frame_num, et le décodeur du pair rend des erreurs de
         * parse en cascade (reordering idc, deblocking params, QP hors bornes).
         * Un échantillon sync sans IDR (trame P marquée intra par l'enregistreur)
         * déclenchait exactement cela. */
        bool prefixParams = intra && videoCodec == VideoCodec::H264 && !videoParamsAvcc.empty()
            && AvccHasNalType( pkt->data, pkt->size, videoNalLengthSize, 0x05 )
            && !AvccHasNalType( pkt->data, pkt->size, videoNalLengthSize, 0x07 );

        if( prefixParams )
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

    // 1) Écouler d'abord les trames audio transcodées déjà prêtes (un paquet
    //    source produit 0..n trames cibles de 20 ms) : émission sans attente.
    if( !audioOutQueue.empty() )
    {
        errcode  = 1;
        waittime = 0;
        return BuildTranscodedAudioFront();
    }

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

    // Cas transcodage audio : décoder+réencoder le paquet source, puis émettre
    // la 1ʳᵉ trame produite (les suivantes le seront au prochain appel via 1)).
    if( audioTranscode && pending->stream_index == audioStreamIdx )
    {
        currentTs = t;
        TranscodeAudioPacket( pending );
        av_packet_unref( pending );
        av_packet_free( &pending );

        if( !audioOutQueue.empty() )
        {
            errcode  = 1;
            waittime = 0;              // écouler la file en rafale, puis lire la suite
            return BuildTranscodedAudioFront();
        }
        // Paquet sans trame complète (démarrage encodeur) : réessayer aussitôt.
        errcode  = 0;
        waittime = 0;
        return NULL;
    }

    // Consommer pending
    bool wasText = ( pending->stream_index == textStreamIdx );
    currentTs = t;
    MediaFrame * f = BuildFrame( pending );
    av_packet_unref( pending );
    av_packet_free( &pending );

    if( f == NULL )
    {
        // Echantillon inexploitable (fichier produit par un enregistreur
        // defaillant) : le sauter et reessayer aussitot plutot que d'avorter
        // toute la lecture. Le paquet est deja consomme, donc pas de boucle.
        errcode  = 0;
        waittime = 0;
        return NULL;
    }
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
    FlushReadAhead();
    seekFloorSet = false;
    av_seek_frame( fmtctx, -1, 0, AVSEEK_FLAG_BACKWARD );
    eofReached     = false;
    schedOffsetSet = false;
    schedOffsetMs  = 0;
    currentTs      = 0;
    if( subConv ) subConv->Reset();     // repartir d'un état RTT vierge
    nextBOMorRepeat = -1;
    ResetAudioTranscode();             // purge FIFO/dec/enc de transcodage
    gettimeofday( &startPlaying, 0 );  // (re)démarre l'horloge de lecture
    return 1;
}

bool Mp4FfReader::Eof()
{
    if( !eofReached || pending != NULL ) return false;

    // Des paquets peuvent rester dans les files de réordonnancement.
    for( std::map< int, std::deque<AVPacket *> >::const_iterator it = readahead.begin();
         it != readahead.end(); it++ )
        if( !it->second.empty() ) return false;

    return true;
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
    FlushReadAhead();
    eofReached     = false;
    schedOffsetSet = false;
    schedOffsetMs  = 0;
    if( subConv ) subConv->Reset();
    nextBOMorRepeat = -1;
    ResetAudioTranscode();
    gettimeofday( &startPlaying, 0 );

    // Peek de la 1re trame pour connaître le temps réel atteint (keyframe).
    // La conversion passe par RawSchedMsOf : depuis le réordonnancement par dts,
    // `pending` n'est pas forcément un paquet de la piste de référence, et
    // l'échelle de temps de `st` ne s'y applique pas (1/8000 vs 1/90000).
    QWORD actualMs = timeMs;
    seekFloorSet = false;

    // Lit jusqu'à obtenir un paquet de la piste de référence : son dts EST le
    // temps réellement atteint (sync frame <= cible), et sert de plancher aux
    // autres pistes.
    while( !eofReached && readahead[refIdx].empty() )
        if( !ReadAhead() ) break;

    if( !readahead[refIdx].empty() )
    {
        long ms      = RawSchedMsOf( readahead[refIdx].front() );
        seekFloorMs  = ms;
        seekFloorSet = true;
        DropBeforeSeekFloor();
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
