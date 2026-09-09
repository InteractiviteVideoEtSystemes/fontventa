/**
 * test_ffmediafilewriter.cpp — FfMediaFileWriter : choix du conteneur par
 * l'extension, table des codecs, aller-retour d'écriture/relecture.
 *
 * La table `IsCodecSupported` est une AFFIRMATION sur ce que les muxers de la
 * ffmpeg du système acceptent. Elle est donc vérifiée ici en écrivant réellement
 * chaque couple (conteneur, codec) : un test qui ne ferait que relire la table
 * ne prouverait rien.
 */
#include <gtest/gtest.h>
#include <medkit/log.h>
#include <medkit/ffmediafilewriter.h>
#include <medkit/audio.h>
#include <medkit/video.h>
#include <medkit/text.h>
#include <medkit/codecs.h>
#include <h264/h264encoder.h>
#include <cstdio>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace {

const int W = 176, H = 144, FPS = 10;

std::string TmpPath( const char * name )
{
    std::string p = "/tmp/libmedkit_ffwriter_";
    p += name;
    return p;
}

// --- Relecture par libavformat -------------------------------------------

struct StreamInfo
{
    int         type;           // AVMEDIA_TYPE_*
    int         codec;          // AV_CODEC_ID_*
    int         width, height;
    int         sampleRate;
    int         extradataSize;
    int         packets;
    long        firstPtsMs;
    long        lastEndMs;
    std::vector<std::string> textPayloads;
};

struct FileInfo
{
    std::string             format;
    std::string             artist;
    std::string             comment;
    std::vector<StreamInfo> streams;
};

// Ouvre le fichier, décrit chaque flux et parcourt tous les paquets.
bool Probe( const std::string & path, FileInfo & out )
{
    AVFormatContext * fmt = NULL;

    if( avformat_open_input( &fmt, path.c_str(), NULL, NULL ) < 0 ) return false;
    if( avformat_find_stream_info( fmt, NULL ) < 0 )
    {
        avformat_close_input( &fmt );
        return false;
    }

    out.format = fmt->iformat->name;

    AVDictionaryEntry * e = av_dict_get( fmt->metadata, "artist", NULL, 0 );
    if( e ) out.artist = e->value;
    e = av_dict_get( fmt->metadata, "comment", NULL, 0 );
    if( e ) out.comment = e->value;

    out.streams.resize( fmt->nb_streams );
    for( unsigned i = 0; i < fmt->nb_streams; i++ )
    {
        AVCodecParameters * par = fmt->streams[i]->codecpar;
        StreamInfo & s = out.streams[i];

        s.type          = par->codec_type;
        s.codec         = par->codec_id;
        s.width         = par->width;
        s.height        = par->height;
        s.sampleRate    = par->sample_rate;
        s.extradataSize = par->extradata_size;
        s.packets       = 0;
        s.firstPtsMs    = -1;
        s.lastEndMs     = 0;
    }

    AVPacket * pkt = av_packet_alloc();
    while( av_read_frame( fmt, pkt ) >= 0 )
    {
        StreamInfo & s = out.streams[pkt->stream_index];
        AVRational   tb = fmt->streams[pkt->stream_index]->time_base;
        AVRational   ms = { 1, 1000 };

        s.packets++;
        if( pkt->pts != AV_NOPTS_VALUE )
        {
            long p = (long)av_rescale_q( pkt->pts, tb, ms );
            if( s.firstPtsMs < 0 ) s.firstPtsMs = p;
            long end = p + (long)av_rescale_q( pkt->duration, tb, ms );
            if( end > s.lastEndMs ) s.lastEndMs = end;
        }
        if( s.type == AVMEDIA_TYPE_SUBTITLE && pkt->size > 0 )
            s.textPayloads.push_back( std::string( (const char *)pkt->data, pkt->size ) );

        av_packet_unref( pkt );
    }
    av_packet_free( &pkt );

    avformat_close_input( &fmt );
    return true;
}

// Le muxer ISOBMFF comble lui-même les trous d'une piste de sous-titres par des
// échantillons tx3g VIDES (2 octets de longueur nulle) : le premier échantillon
// relu n'est donc pas forcément le premier qu'on a écrit.
const std::string * FirstNonEmptyText( const StreamInfo & s, size_t minSize )
{
    for( size_t i = 0; i < s.textPayloads.size(); i++ )
        if( s.textPayloads[i].size() > minSize ) return &s.textPayloads[i];
    return NULL;
}

const StreamInfo * FindStream( const FileInfo & f, int type )
{
    for( size_t i = 0; i < f.streams.size(); i++ )
        if( f.streams[i].type == type ) return &f.streams[i];
    return NULL;
}

// --- Producteurs de trames ------------------------------------------------

std::vector<SWORD> MakeTone( int nb, int rate )
{
    std::vector<SWORD> pcm( nb );
    for( int i = 0; i < nb; i++ )
        pcm[i] = (SWORD)( 8000.0 * sin( 2.0 * M_PI * 440.0 * i / rate ) );
    return pcm;
}

/*
 * Trames audio RÉELLEMENT encodées quand la ffmpeg locale sait le faire.
 *
 * Une charge utile arbitraire ne suffit pas : le muxer ISOBMFF inspecte les
 * paquets (aac_adtstoasc sur un octet de synchro ADTS, découpage des paquets
 * Opus) et rejette ce qu'il ne sait pas lire. Un test à charge utile bidon
 * conclurait donc « ce codec ne passe pas » alors que seul le test est faux.
 *
 * @return nombre de trames remises au writer.
 */
int PushAudio( FfMediaFileWriter & w, AudioCodec::Type codec, DWORD rate, int n )
{
    Properties props;
    std::unique_ptr<AudioEncoder> enc( AudioCodecFactory::CreateEncoder( codec, props ) );

    int pushed = 0;

    if( enc.get() != NULL )
    {
        DWORD encRate = enc->GetRate() > 0 ? enc->GetRate() : rate;
        DWORD nb      = encRate / 50;                    // tranches de 20 ms

        // Boucle sur les TRANCHES fournies, pas sur les trames attendues : un
        // encodeur accumule (AAC réunit 1024 échantillons, soit 128 ms à
        // 8 kHz), donc 20 ms d'entrée ne donnent pas 20 ms de sortie.
        for( int i = 0; i < n * 20 && pushed < n; i++ )
        {
            std::vector<SWORD> pcm = MakeTone( (int)nb, (int)encRate );
            SamplesPtr s = Samples::FromBuffer( &pcm[0], nb, encRate );
            if( !s ) break;

            AudioFramePtr af = enc->EncodeFrame( s );
            if( !af ) continue;                          // l'encodeur accumule

            af->SetTimestamp( (DWORD)( pushed * 20 ) );  // millisecondes
            if( w.ProcessFrame( af.get() ) == 1 ) pushed++;
        }
        return pushed;
    }

    // Codecs par échantillon sans encodeur dans la fabrique (SLIN) : la charge
    // utile est le signal lui-même, aucun muxer ne la parse.
    for( int i = 0; i < n; i++ )
    {
        AudioFrame af( codec, rate );
        std::vector<SWORD> pcm = MakeTone( (int)( rate / 50 ), (int)rate );
        af.SetMedia( (BYTE *)&pcm[0], pcm.size() * sizeof( SWORD ) );
        af.SetTimestamp( (DWORD)( i * 20 ) );
        if( w.ProcessFrame( &af ) == 1 ) pushed++;
    }
    return pushed;
}

void PushDummyVideo( FfMediaFileWriter & w, VideoCodec::Type codec, int n )
{
    for( int i = 0; i < n; i++ )
    {
        VideoFrame vf( codec, 256 );
        std::vector<BYTE> payload( 200, (BYTE)( i + 1 ) );
        vf.SetMedia( &payload[0], payload.size() );
        vf.SetWidth( W );
        vf.SetHeight( H );
        vf.SetIntra( i == 0 );
        vf.SetTimestamp( (DWORD)( i * ( 90000 / FPS ) ) );   // horloge 90 kHz
        w.ProcessFrame( &vf );
    }
}

// Vraie vidéo H264 (SPS/PPS présents) : c'est la seule façon d'exercer la
// construction de l'avcC.
bool PushRealH264( FfMediaFileWriter & w, int n, int & written )
{
    Properties   props;
    H264Encoder  enc( props );

    enc.SetFrameRate( FPS, 256, FPS );
    if( enc.SetSize( W, H ) < 0 ) return false;

    written = 0;
    for( int i = 0; i < n; i++ )
    {
        PictPtr pic = Pict::CreateColor( W, H, 16 + ( i * 8 ) % 200, 128, 128 );
        if( !pic ) return false;

        VideoFramePtr vf = enc.EncodeFrame( pic );
        if( !vf ) continue;

        vf->SetTimestamp( (DWORD)( i * ( 90000 / FPS ) ) );
        if( w.ProcessFrame( vf.get() ) == 1 ) written++;
    }
    return written > 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Choix du conteneur
// ---------------------------------------------------------------------------

TEST( FfMediaFileWriter, LExtensionChoisitLeConteneur )
{
    EXPECT_EQ( FfMediaFileWriter::ContainerMp4,      FfMediaFileWriter::GetContainerFor( "/tmp/a.mp4" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerMov,      FfMediaFileWriter::GetContainerFor( "/tmp/a.mov" ) );
    EXPECT_EQ( FfMediaFileWriter::Container3gp,      FfMediaFileWriter::GetContainerFor( "/tmp/a.3gp" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerMatroska, FfMediaFileWriter::GetContainerFor( "/tmp/a.mkv" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerMatroska, FfMediaFileWriter::GetContainerFor( "/tmp/a.MKV" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerMatroska, FfMediaFileWriter::GetContainerFor( "/tmp/a.mka" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerWebm,     FfMediaFileWriter::GetContainerFor( "/tmp/a.webm" ) );

    // Une extension inconnue n'est pas devinée : le conteneur décide du format
    // d'échantillon des sous-titres et de la table de codecs.
    EXPECT_EQ( FfMediaFileWriter::ContainerUnknown, FfMediaFileWriter::GetContainerFor( "/tmp/a.avi" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerUnknown, FfMediaFileWriter::GetContainerFor( "/tmp/sans_extension" ) );
    EXPECT_EQ( FfMediaFileWriter::ContainerUnknown, FfMediaFileWriter::GetContainerFor( NULL ) );
}

TEST( FfMediaFileWriter, UneExtensionInconnueNOuvrePas )
{
    std::string path = TmpPath( "refus.avi" );
    ::unlink( path.c_str() );

    FfMediaFileWriter w( NULL, path.c_str(), false );
    EXPECT_FALSE( w.IsOpen() );
    EXPECT_EQ( -1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );
}

// ---------------------------------------------------------------------------
// Table des codecs : ce que les muxers acceptent RÉELLEMENT
// ---------------------------------------------------------------------------

namespace {

// Écrit un fichier avec une seule piste audio et rend le nombre de paquets
// relus. -1 = la piste a été refusée, -2 = le fichier est illisible,
// -3 = la ffmpeg locale n'encode pas ce codec (rien à conclure).
int WriteAudioOnly( FfMediaFileWriter::Container c, const char * ext, AudioCodec::Type codec )
{
    std::string path = TmpPath( "codec" ) + "." + ext;
    ::unlink( path.c_str() );

    int pushed = 0;
    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        if( !w.IsOpen() ) return -2;
        if( w.AddTrack( codec, 8000, "audio" ) != 1 ) return -1;
        pushed = PushAudio( w, codec, 8000, 10 );
        w.Close();
    }

    if( pushed == 0 ) return -3;

    FileInfo info;
    if( !Probe( path, info ) ) return -2;
    ::unlink( path.c_str() );

    const StreamInfo * s = FindStream( info, AVMEDIA_TYPE_AUDIO );
    return s ? s->packets : 0;
}

int WriteVideoOnly( FfMediaFileWriter::Container c, const char * ext, VideoCodec::Type codec )
{
    std::string path = TmpPath( "codec" ) + "." + ext;
    ::unlink( path.c_str() );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        if( !w.IsOpen() ) return -2;
        if( w.AddTrack( codec, W, H, 256, "video" ) != 1 ) return -1;

        if( codec == VideoCodec::H264 )
        {
            int written = 0;
            if( !PushRealH264( w, 10, written ) ) return -2;
        }
        else
        {
            PushDummyVideo( w, codec, 10 );
        }
        w.Close();
    }

    FileInfo info;
    if( !Probe( path, info ) ) return -2;
    ::unlink( path.c_str() );

    const StreamInfo * s = FindStream( info, AVMEDIA_TYPE_VIDEO );
    return s ? s->packets : 0;
}

struct AudioCase { FfMediaFileWriter::Container c; const char * ext; AudioCodec::Type codec; };
struct VideoCase { FfMediaFileWriter::Container c; const char * ext; VideoCodec::Type codec; };

} // namespace

TEST( FfMediaFileWriter, LaTableAudioDitVraiPourChaqueConteneur )
{
    const AudioCase cases[] = {
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::AAC   },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::OPUS  },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::PCMU  },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::PCMA  },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::AMR   },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  AudioCodec::G722  },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::PCMU  },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::PCMA  },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::SLIN  },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::AMR   },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::AAC   },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::OPUS  },
        { FfMediaFileWriter::ContainerMov,      "mov",  AudioCodec::G722  },
        { FfMediaFileWriter::Container3gp,      "3gp",  AudioCodec::AMR   },
        { FfMediaFileWriter::Container3gp,      "3gp",  AudioCodec::AAC   },
        { FfMediaFileWriter::Container3gp,      "3gp",  AudioCodec::PCMU  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::PCMU  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::PCMA  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::SLIN  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::G722  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::AMR   },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::AAC   },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::OPUS  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  AudioCodec::GSM   },
        { FfMediaFileWriter::ContainerWebm,     "webm", AudioCodec::OPUS  },
        { FfMediaFileWriter::ContainerWebm,     "webm", AudioCodec::PCMU  },
    };

    for( size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ )
    {
        const AudioCase & t = cases[i];
        bool claimed = FfMediaFileWriter::IsCodecSupported( t.c, t.codec );
        int  packets = WriteAudioOnly( t.c, t.ext, t.codec );

        if( packets == -3 )
        {
            Log( "test: %s non encodable par la ffmpeg locale, couple non conclu.\n",
                 AudioCodec::GetNameFor( t.codec ) );
            continue;
        }

        if( claimed )
            EXPECT_GT( packets, 0 ) << "la table annonce " << AudioCodec::GetNameFor( t.codec )
                                    << " en ." << t.ext << " mais l'écriture ne rend rien";
        else
            EXPECT_EQ( -1, packets ) << AudioCodec::GetNameFor( t.codec ) << " en ." << t.ext
                                     << " : refus attendu à l'ouverture de la piste";
    }
}

TEST( FfMediaFileWriter, LaTableVideoDitVraiPourChaqueConteneur )
{
    const VideoCase cases[] = {
        { FfMediaFileWriter::ContainerMp4,      "mp4",  VideoCodec::H264       },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  VideoCodec::MPEG4      },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  VideoCodec::H263_1996  },
        { FfMediaFileWriter::ContainerMp4,      "mp4",  VideoCodec::VP8        },
        { FfMediaFileWriter::ContainerMov,      "mov",  VideoCodec::H264       },
        { FfMediaFileWriter::ContainerMov,      "mov",  VideoCodec::H263_1996  },
        { FfMediaFileWriter::ContainerMov,      "mov",  VideoCodec::MPEG4      },
        { FfMediaFileWriter::ContainerMov,      "mov",  VideoCodec::VP8        },
        { FfMediaFileWriter::Container3gp,      "3gp",  VideoCodec::H263_1996  },
        { FfMediaFileWriter::Container3gp,      "3gp",  VideoCodec::H264       },
        { FfMediaFileWriter::Container3gp,      "3gp",  VideoCodec::VP8        },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  VideoCodec::H264       },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  VideoCodec::H263_1996  },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  VideoCodec::MPEG4      },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  VideoCodec::VP8        },
        { FfMediaFileWriter::ContainerMatroska, "mkv",  VideoCodec::H263_1998  },
        { FfMediaFileWriter::ContainerWebm,     "webm", VideoCodec::VP8        },
        { FfMediaFileWriter::ContainerWebm,     "webm", VideoCodec::H264       },
    };

    for( size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ )
    {
        const VideoCase & t = cases[i];
        bool claimed = FfMediaFileWriter::IsCodecSupported( t.c, t.codec );
        int  packets = WriteVideoOnly( t.c, t.ext, t.codec );

        if( claimed )
            EXPECT_GT( packets, 0 ) << "la table annonce " << VideoCodec::GetNameFor( t.codec )
                                    << " en ." << t.ext << " mais l'écriture ne rend rien";
        else
            EXPECT_EQ( -1, packets ) << VideoCodec::GetNameFor( t.codec ) << " en ." << t.ext
                                     << " : refus attendu à l'ouverture de la piste";
    }
}

TEST( FfMediaFileWriter, LeMuxerMp4RefuseLesCodecsTelecom )
{
    // Le piège que cette classe doit rendre visible AVANT l'enregistrement :
    // les codecs de tous les appels réels ne rentrent pas dans un .mp4.
    EXPECT_FALSE( FfMediaFileWriter::IsCodecSupported( FfMediaFileWriter::ContainerMp4, AudioCodec::PCMU ) );
    EXPECT_TRUE ( FfMediaFileWriter::IsCodecSupported( FfMediaFileWriter::ContainerMatroska, AudioCodec::PCMU ) );
    EXPECT_TRUE ( FfMediaFileWriter::IsCodecSupported( FfMediaFileWriter::ContainerMov, AudioCodec::PCMU ) );

    std::string path = TmpPath( "telecom.mp4" );
    ::unlink( path.c_str() );

    FfMediaFileWriter w( NULL, path.c_str(), false );
    ASSERT_TRUE( w.IsOpen() );
    EXPECT_FALSE( w.IsCodecSupported( AudioCodec::PCMU ) );
    EXPECT_EQ( -1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );

    ::unlink( path.c_str() );
}

// ---------------------------------------------------------------------------
// Aller-retour complet
// ---------------------------------------------------------------------------

TEST( FfMediaFileWriter, MatroskaPorteAudioTelecomVideoEtTexte )
{
    std::string path = TmpPath( "complet.mkv" );
    ::unlink( path.c_str() );

    int videoWritten = 0, audioWritten = 0;
    {
        FfMediaFileWriter w( NULL, path.c_str(), /*waitVideo*/ false );
        ASSERT_TRUE( w.IsOpen() );
        w.SetParticipantName( "Emmanuel" );

        ASSERT_EQ( 1, w.AddTrack( VideoCodec::H264, W, H, 256, "video" ) );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );
        ASSERT_EQ( 1, w.AddTrack( TextCodec::T140, "texte", -1 ) );

        ASSERT_TRUE( PushRealH264( w, 10, videoWritten ) );
        audioWritten = PushAudio( w, AudioCodec::PCMU, 8000, 10 );
        ASSERT_GT( audioWritten, 0 );

        for( int i = 0; i < 3; i++ )
        {
            std::wstring line = L"bonjour";
            TextFrame tf( (DWORD)( 500 + i * 500 ), line );
            EXPECT_EQ( 1, w.ProcessFrame( &tf ) );
        }

        w.Flush();
        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) ) << "fichier illisible";
    EXPECT_EQ( "matroska,webm", info.format );
    EXPECT_EQ( "Emmanuel", info.artist );
    ASSERT_EQ( 3u, info.streams.size() );

    const StreamInfo * v = FindStream( info, AVMEDIA_TYPE_VIDEO );
    const StreamInfo * a = FindStream( info, AVMEDIA_TYPE_AUDIO );
    const StreamInfo * t = FindStream( info, AVMEDIA_TYPE_SUBTITLE );

    ASSERT_TRUE( v != NULL );
    EXPECT_EQ( AV_CODEC_ID_H264, v->codec );
    EXPECT_EQ( W, v->width );
    EXPECT_EQ( H, v->height );
    // avcC construit à partir du SPS/PPS de la première trame : sans lui, aucun
    // décodeur ne s'initialise sur le fichier.
    EXPECT_GT( v->extradataSize, 0 );
    EXPECT_EQ( videoWritten, v->packets );

    ASSERT_TRUE( a != NULL );
    EXPECT_EQ( AV_CODEC_ID_PCM_MULAW, a->codec );
    EXPECT_EQ( 8000, a->sampleRate );
    EXPECT_EQ( audioWritten, a->packets );

    ASSERT_TRUE( t != NULL );
    EXPECT_EQ( AV_CODEC_ID_SUBRIP, t->codec );
    EXPECT_GT( t->packets, 0 );
    // S_TEXT/UTF8 : le texte nu, sans préfixe de longueur.
    const std::string * sub = FirstNonEmptyText( *t, 0 );
    ASSERT_TRUE( sub != NULL );
    EXPECT_NE( std::string::npos, sub->find( "bonjour" ) )
        << "sous-titre relu : [" << *sub << "]";

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, Mp4PorteAacEtH264 )
{
    std::string path = TmpPath( "complet.mp4" );
    ::unlink( path.c_str() );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );

        ASSERT_EQ( 1, w.AddTrack( VideoCodec::H264, W, H, 256, "video" ) );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::AAC, 8000, "audio" ) );

        int written = 0;
        ASSERT_TRUE( PushRealH264( w, 10, written ) );
        ASSERT_GT( PushAudio( w, AudioCodec::AAC, 8000, 10 ), 0 );

        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );
    ASSERT_EQ( 2u, info.streams.size() );

    const StreamInfo * v = FindStream( info, AVMEDIA_TYPE_VIDEO );
    const StreamInfo * a = FindStream( info, AVMEDIA_TYPE_AUDIO );

    ASSERT_TRUE( v != NULL );
    EXPECT_EQ( AV_CODEC_ID_H264, v->codec );
    EXPECT_GT( v->extradataSize, 0 );

    ASSERT_TRUE( a != NULL );
    EXPECT_EQ( AV_CODEC_ID_AAC, a->codec );
    // AudioSpecificConfig : sans elle, la piste AAC est indécodable.
    EXPECT_GT( a->extradataSize, 0 );

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, LAv1PorteSonSequenceHeaderEnExtradata )
{
    Properties props;
    props.SetProperty( "av1.preset", "12" );        // le plus rapide, c'est un test

    std::unique_ptr<VideoEncoder> enc( VideoCodecFactory::CreateEncoder( VideoCodec::AV1, props ) );
    if( enc.get() == NULL ) GTEST_SKIP() << "pas d'encodeur AV1 dans la ffmpeg locale";

    enc->SetFrameRate( FPS, 256, FPS );
    ASSERT_GE( enc->SetSize( W, H ), 0 );

    std::string path = TmpPath( "av1.mkv" );
    ::unlink( path.c_str() );

    int written = 0;
    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( VideoCodec::AV1, W, H, 256, "video" ) );

        // SVT-AV1 accumule (lookahead) : il faut lui pousser bien plus d'images
        // qu'on n'en attend en retour avant qu'il n'émette la première.
        for( int i = 0; i < 60; i++ )
        {
            PictPtr pic = Pict::CreateColor( W, H, 16 + ( i * 20 ) % 200, 128, 128 );
            ASSERT_TRUE( pic != NULL );

            VideoFramePtr vf = enc->EncodeFrame( pic );
            if( !vf ) continue;

            vf->SetTimestamp( (DWORD)( i * ( 90000 / FPS ) ) );
            if( w.ProcessFrame( vf.get() ) == 1 ) written++;
        }
        EXPECT_EQ( 0, w.Close() );
    }

    ASSERT_GT( written, 0 ) << "l'encodeur AV1 n'a rien produit";

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );

    const StreamInfo * v = FindStream( info, AVMEDIA_TYPE_VIDEO );
    ASSERT_TRUE( v != NULL );
    EXPECT_EQ( AV_CODEC_ID_AV1, v->codec );
    // av1C : reconstruit depuis le sequence header OBU de la première trame.
    EXPECT_GT( v->extradataSize, 0 );
    EXPECT_EQ( written, v->packets );

    ::unlink( path.c_str() );
}

// ---------------------------------------------------------------------------
// Sous-titres au fil de l'eau
// ---------------------------------------------------------------------------

namespace {

// Recopie les octets déjà écrits d'un fichier encore ouvert par le writer.
bool CopyFileNow( const std::string & from, const std::string & to )
{
    FILE * in = fopen( from.c_str(), "rb" );
    if( in == NULL ) return false;

    FILE * out = fopen( to.c_str(), "wb" );
    if( out == NULL ) { fclose( in ); return false; }

    char   buf[4096];
    size_t n;
    while( ( n = fread( buf, 1, sizeof( buf ), in ) ) > 0 ) fwrite( buf, 1, n, out );

    fclose( in );
    fclose( out );
    return true;
}

} // namespace

TEST( FfMediaFileWriter, LesSousTitresSontDansLeFichierAvantLaFermeture )
{
    std::string path = TmpPath( "filoeau.mkv" );
    std::string copy = TmpPath( "filoeau_copie.mkv" );
    ::unlink( path.c_str() );
    ::unlink( copy.c_str() );

    // EXIGENCE : le texte est enregistré au fil de l'eau. Un enregistrement
    // interrompu -- pas de Close(), donc pas de trailer -- doit contenir tout ce
    // qui a été tapé avant l'interruption.
    std::unique_ptr<FfMediaFileWriter> w( new FfMediaFileWriter( NULL, path.c_str(), false ) );
    ASSERT_TRUE( w->IsOpen() );
    ASSERT_EQ( 1, w->AddTrack( AudioCodec::PCMU, 8000, "audio" ) );
    ASSERT_EQ( 1, w->AddTrack( TextCodec::T140, "texte", -1 ) );

    ASSERT_GT( PushAudio( *w, AudioCodec::PCMU, 8000, 5 ), 0 );

    const int NBLIGNES = 3;
    for( int i = 0; i < NBLIGNES; i++ )
    {
        std::wstring line = L"bonjour";
        TextFrame tf( (DWORD)( 300 + i * 300 ), line );
        ASSERT_EQ( 1, w->ProcessFrame( &tf ) );
    }

    // Le writer n'est ni vidé ni fermé : on relit une copie de ce qui est déjà
    // sur le disque.
    ASSERT_TRUE( CopyFileNow( path, copy ) );

    FileInfo info;
    ASSERT_TRUE( Probe( copy, info ) ) << "fichier partiel illisible";

    const StreamInfo * t = FindStream( info, AVMEDIA_TYPE_SUBTITLE );
    ASSERT_TRUE( t != NULL );
    EXPECT_EQ( NBLIGNES, (int)t->textPayloads.size() )
        << "un échantillon par frappe doit déjà être écrit, sans attendre la suivante";

    const std::string * sub = FirstNonEmptyText( *t, 0 );
    ASSERT_TRUE( sub != NULL );
    EXPECT_NE( std::string::npos, sub->find( "bonjour" ) );

    w.reset();
    ::unlink( path.c_str() );
    ::unlink( copy.c_str() );
}

TEST( FfMediaFileWriter, LeFichierTexteAnnexeSeRemplitLigneParLigne )
{
    std::string path = TmpPath( "annexe.mp4" );
    std::string txt  = TmpPath( "annexe.txt" );
    ::unlink( path.c_str() );
    ::unlink( txt.c_str() );

    int fd = ::open( txt.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600 );
    ASSERT_GE( fd, 0 );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( TextCodec::T140, "texte", fd ) );

        // Chaque ligne terminée part au fichier annexe dès la frappe du saut de
        // ligne, sans attendre la fermeture.
        const wchar_t * lignes[] = { L"premiere\n", L"deuxieme\n" };
        for( int i = 0; i < 2; i++ )
        {
            TextFrame tf( (DWORD)( 200 + i * 400 ), std::wstring( lignes[i] ) );
            ASSERT_EQ( 1, w.ProcessFrame( &tf ) );
        }

        char   buf[512];
        ::lseek( fd, 0, SEEK_SET );
        ssize_t n = ::read( fd, buf, sizeof( buf ) - 1 );
        ASSERT_GT( n, 0 ) << "le fichier annexe est vide avant la fermeture";
        buf[n] = 0;
        EXPECT_NE( (char *)NULL, strstr( buf, "premiere" ) );
        EXPECT_NE( (char *)NULL, strstr( buf, "deuxieme" ) );

        // Position d'écriture rendue à la fin : la lecture ci-dessus l'a déplacée.
        ::lseek( fd, n, SEEK_SET );

        EXPECT_EQ( 0, w.Close() );
    }

    // À la fermeture, ce texte est recopié dans le tag `comment` (ISOBMFF).
    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );
    EXPECT_NE( std::string::npos, info.comment.find( "premiere" ) )
        << "comment relu : [" << info.comment << "]";

    ::close( fd );
    ::unlink( path.c_str() );
    ::unlink( txt.c_str() );
}

TEST( FfMediaFileWriter, LeDernierSousTitreEstTenuJusquALaFin )
{
    std::string path = TmpPath( "dernier.mkv" );
    ::unlink( path.c_str() );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( TextCodec::T140, "texte", -1 ) );

        std::wstring line = L"salut";
        TextFrame tf( 0, line );
        ASSERT_EQ( 1, w.ProcessFrame( &tf ) );

        // La frappe n'a duré que 100 ms (durée d'amorce) ; l'enregistrement
        // continue. Sans le dernier échantillon de Flush, le texte disparaîtrait
        // de l'écran juste après la frappe.
        usleep( 300000 );
        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );

    const StreamInfo * t = FindStream( info, AVMEDIA_TYPE_SUBTITLE );
    ASSERT_TRUE( t != NULL );
    EXPECT_EQ( 2u, t->textPayloads.size() ) << "la frappe, puis le maintien final";
    EXPECT_GE( t->lastEndMs, 300 );

    ::unlink( path.c_str() );
}

// ---------------------------------------------------------------------------
// Ligne de temps
// ---------------------------------------------------------------------------

TEST( FfMediaFileWriter, LeDelaiInitialDecaleLaPremiereTrame )
{
    std::string path = TmpPath( "delai.mkv" );
    ::unlink( path.c_str() );

    int pushed = 0;
    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );

        // Participant arrivé 2 s après le début de l'enregistrement : le trou
        // est exprimé par le conteneur, PAS comblé par du silence.
        w.SetInitialDelay( 2000 );
        pushed = PushAudio( w, AudioCodec::PCMU, 8000, 25 );
        ASSERT_GT( pushed, 0 );
        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );

    const StreamInfo * a = FindStream( info, AVMEDIA_TYPE_AUDIO );
    ASSERT_TRUE( a != NULL );
    EXPECT_EQ( pushed, a->packets );
    ASSERT_EQ( 25, pushed );
    // 2 s de décalage, plus le temps réel écoulé depuis l'ouverture (quelques
    // ms en test) : la borne haute laisse cette marge.
    EXPECT_GE( a->firstPtsMs, 2000 );
    EXPECT_LT( a->firstPtsMs, 2500 );
    // 25 trames de 20 ms après le décalage.
    EXPECT_GE( a->lastEndMs, 2480 );

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, LaPisteDemandeeApresLaPremiereTrameEstRefusee )
{
    std::string path = TmpPath( "tardive.mkv" );
    ::unlink( path.c_str() );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );

        PushAudio( w, AudioCodec::PCMU, 8000, 5 );

        // libavformat a figé l'en-tête : contrairement à mp4writer, aucune piste
        // ne peut naître ensuite -- et la trame correspondante rend -3.
        EXPECT_EQ( -1, w.AddTrack( VideoCodec::H264, W, H, 256, "video" ) );

        VideoFrame vf( VideoCodec::H264, 256 );
        std::vector<BYTE> payload( 100, 0x65 );
        vf.SetMedia( &payload[0], payload.size() );
        vf.SetIntra( true );
        EXPECT_EQ( -3, w.ProcessFrame( &vf ) );

        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );
    EXPECT_EQ( 1u, info.streams.size() );

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, SansPisteVideoDeclareeLaTrameVideoEstRefusee )
{
    std::string path = TmpPath( "sansvideo.mkv" );
    ::unlink( path.c_str() );

    FfMediaFileWriter w( NULL, path.c_str(), false );
    ASSERT_TRUE( w.IsOpen() );

    // Pas d'auto-création de piste : c'est la différence de contrat avec
    // mp4writer, et elle doit être visible par le code de retour.
    VideoFrame vf( VideoCodec::H264, 256 );
    std::vector<BYTE> payload( 100, 0x65 );
    vf.SetMedia( &payload[0], payload.size() );
    vf.SetIntra( true );
    EXPECT_EQ( -3, w.ProcessFrame( &vf ) );

    AudioFrame af( AudioCodec::PCMU, 8000 );
    std::vector<BYTE> pcm( 160, 0xFF );
    af.SetMedia( &pcm[0], pcm.size() );
    EXPECT_EQ( -3, w.ProcessFrame( &af ) );

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, UneTrameDeCodecEtrangerEstRefusee )
{
    std::string path = TmpPath( "mauvaiscodec.mkv" );
    ::unlink( path.c_str() );

    FfMediaFileWriter w( NULL, path.c_str(), false );
    ASSERT_TRUE( w.IsOpen() );
    ASSERT_EQ( 1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );

    AudioFrame af( AudioCodec::PCMA, 8000 );
    std::vector<BYTE> pcm( 160, 0xD5 );
    af.SetMedia( &pcm[0], pcm.size() );
    af.SetTimestamp( 0 );
    EXPECT_EQ( -1, w.ProcessFrame( &af ) );

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, WaitVideoRetientAudioEtTexte )
{
    std::string path = TmpPath( "waitvideo.mkv" );
    ::unlink( path.c_str() );

    int pushedAfterVideo = 0;
    {
        FfMediaFileWriter w( NULL, path.c_str(), /*waitVideo*/ true );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( VideoCodec::H264, W, H, 256, "video" ) );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::PCMU, 8000, "audio" ) );

        EXPECT_EQ( 0, w.IsVideoStarted() );

        // Tant que la vidéo n'a pas démarré, l'audio est écarté (retour 0).
        AudioFrame af( AudioCodec::PCMU, 8000 );
        std::vector<BYTE> pcm( 160, 0xFF );
        af.SetMedia( &pcm[0], pcm.size() );
        af.SetTimestamp( 0 );
        EXPECT_EQ( 0, w.ProcessFrame( &af ) );

        int written = 0;
        ASSERT_TRUE( PushRealH264( w, 10, written ) );
        EXPECT_EQ( 1, w.IsVideoStarted() );

        int audioWritten = PushAudio( w, AudioCodec::PCMU, 8000, 10 );
        EXPECT_EQ( 0, w.Close() );
        pushedAfterVideo = audioWritten;
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );

    const StreamInfo * a = FindStream( info, AVMEDIA_TYPE_AUDIO );
    ASSERT_TRUE( a != NULL );
    EXPECT_EQ( pushedAfterVideo, a->packets ) << "la trame audio antérieure à la vidéo devait être écartée";

    ::unlink( path.c_str() );
}

TEST( FfMediaFileWriter, LeTexteEnMp4PorteSonPrefixeDeLongueur )
{
    std::string path = TmpPath( "texte.mp4" );
    ::unlink( path.c_str() );

    {
        FfMediaFileWriter w( NULL, path.c_str(), false );
        ASSERT_TRUE( w.IsOpen() );
        ASSERT_EQ( 1, w.AddTrack( AudioCodec::AAC, 8000, "audio" ) );
        ASSERT_EQ( 1, w.AddTrack( TextCodec::T140, "texte", -1 ) );

        ASSERT_GT( PushAudio( w, AudioCodec::AAC, 8000, 10 ), 0 );
        for( int i = 0; i < 3; i++ )
        {
            std::wstring line = L"salut";
            TextFrame tf( (DWORD)( 200 + i * 400 ), line );
            EXPECT_EQ( 1, w.ProcessFrame( &tf ) );
        }
        w.Flush();
        EXPECT_EQ( 0, w.Close() );
    }

    FileInfo info;
    ASSERT_TRUE( Probe( path, info ) );

    const StreamInfo * t = FindStream( info, AVMEDIA_TYPE_SUBTITLE );
    ASSERT_TRUE( t != NULL );
    EXPECT_EQ( AV_CODEC_ID_MOV_TEXT, t->codec );

    const std::string * sub = FirstNonEmptyText( *t, 2 );
    ASSERT_TRUE( sub != NULL ) << t->textPayloads.size() << " échantillons, tous vides";

    // tx3g : [longueur sur 2 octets big endian][UTF-8]
    const unsigned char * p = (const unsigned char *)sub->data();
    DWORD declared = ( (DWORD)p[0] << 8 ) | p[1];
    EXPECT_EQ( sub->size() - 2, declared );
    EXPECT_NE( std::string::npos, sub->find( "salut" ) );

    ::unlink( path.c_str() );
}
