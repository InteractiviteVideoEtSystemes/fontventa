// Harnais P1 — teste Mp4FfReader (ouverture, détection codec, métadonnées,
// AVCDescriptor) sur un fichier MP4 réel. Lié contre libmedkit.a.
//   Usage: ffmp4probe fichier.mp4
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "medkit/log.h"
#include "medkit/text.h"
#include "medkit/ffmp4reader.h"

static double nowSec()
{
    struct timeval tv; gettimeofday( &tv, 0 );
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main( int argc, char ** argv )
{
    if( argc < 2 ) { fprintf( stderr, "usage: %s fichier.mp4\n", argv[0] ); return 2; }

    Mp4FfReader reader( argv[1] );
    if( !reader.IsOpen() ) { fprintf( stderr, "ouverture échouée\n" ); return 1; }

    // Mêmes listes de codecs que MP4Streamer::Open (mcu/src/mp4streamer.cpp)
    AudioCodec::Type acodecs[] = { AudioCodec::PCMU, AudioCodec::PCMA, AudioCodec::AMR,
                                   AudioCodec::OPUS, AudioCodec::GSM,  AudioCodec::G722 };
    VideoCodec::Type vcodecs[] = { VideoCodec::H264, VideoCodec::H263_1998,
                                   VideoCodec::H263_1996, VideoCodec::VP8 };

    int aok = reader.OpenTrack( acodecs, sizeof(acodecs)/sizeof(acodecs[0]), AudioCodec::PCMU, false );
    int vok = reader.OpenTrack( vcodecs, sizeof(vcodecs)/sizeof(vcodecs[0]), VideoCodec::H264, false, false );
    // Texte : T.140 nu (comme MP4Streamer::Open) ; passer "red" en 2e arg CLI force T140RED
    bool useRed = ( argc >= 3 && strcmp( argv[2], "red" ) == 0 );
    int tok = reader.OpenTrack( useRed ? TextCodec::T140RED : TextCodec::T140, 106, 1 );

    printf( "\n== bilan P1 ==\n" );
    printf( "OpenTrack audio=%d video=%d text=%d (%s)\n", aok, vok, tok, useRed ? "T140RED" : "T140" );
    printf( "HasAudio=%d HasVideo=%d HasText=%d\n",
            reader.HasAudioTrack(), reader.HasVideoTrack(), reader.HasTextTrack() );
    printf( "duration=%.3f s\n", reader.GetDuration() );

    if( reader.HasVideoTrack() )
    {
        VideoCodec::Type vc; reader.GetVideoCodec( vc );
        printf( "video: codec=%s %ux%u fps=%.3f bitrate=%u\n",
                VideoCodec::GetNameFor( vc ),
                reader.GetVideoWidth(), reader.GetVideoHeight(),
                reader.GetVideoFramerate(), reader.GetVideoBitrate() );

        AVCDescriptor * d = reader.GetAVCDescriptor();
        if( d )
        {
            printf( "avcC: version=%u profile=0x%02x level=0x%02x nalLen=%u SPS=%u PPS=%u\n",
                    d->GetConfigurationVersion(), d->GetAVCProfileIndication(),
                    d->GetAVCLevelIndication(), d->GetNALUnitLength() + 1,
                    d->GetNumOfSequenceParameterSets(), d->GetNumOfPictureParameterSets() );
            delete d;
        }
        else printf( "avcC: (aucun)\n" );
    }

    if( reader.HasAudioTrack() )
    {
        AudioCodec::Type ac; reader.GetCodec( ac );
        printf( "audio: codec=%s\n", AudioCodec::GetNameFor( ac ) );
    }

    // -------- Mode rapide : ffmp4probe fichier.mp4 <seekMs> --------
    if( argc >= 3 && !useRed )
    {
        QWORD seekMs = (QWORD)atoll( argv[2] );
        QWORD pre    = reader.PreSeek( seekMs );
        QWORD actual = reader.Seek( seekMs );
        int errcode = 0; unsigned long waittime = 0;
        MediaFrame * f = reader.GetNextFrame( errcode, waittime );
        bool intra = ( f && f->GetType() == MediaFrame::Video ) ? ((VideoFrame*)f)->IsIntra() : false;
        printf( "\n== seek rapide ==\n demandé=%llu PreSeek=%llu Seek(actual)=%llu  1re trame intra=%d ts90k=%u\n",
                (unsigned long long)seekMs, (unsigned long long)pre, (unsigned long long)actual,
                intra, f ? f->GetTimeStamp() : 0 );
        return 0;
    }

    // -------- P2 : pilotage cadencé de GetNextFrame (comme MP4Streamer::PlayLoop) --------
    printf( "\n== P2 : lecture cadencée ==\n" );
    reader.Rewind();

    int    nFrames = 0, nIntra = 0, nRtp = 0, nVideo = 0, nAudio = 0, nText = 0;
    DWORD  maxPayload = 0;
    DWORD  firstIntraLen = 0;
    double t0 = nowSec();

    for(;;)
    {
        int errcode = 0; unsigned long waittime = 0;
        MediaFrame * f = reader.GetNextFrame( errcode, waittime );

        if( errcode == -1 ) break; // EOF

        if( f )
        {
            nFrames++;
            if( f->GetType() == MediaFrame::Video )
            {
                nVideo++;
                if( ((VideoFrame*)f)->IsIntra() ) { if( !nIntra ) firstIntraLen = f->GetLength(); nIntra++; }
            }
            else if( f->GetType() == MediaFrame::Audio ) nAudio++;
            else if( f->GetType() == MediaFrame::Text )
            {
                nText++;
                TextFrame * tf = (TextFrame *)f;
                if( nText <= 8 )
                    printf( "  texte#%d ts=%u wlen=%u \"%ls\"\n",
                            nText, tf->GetTimeStamp(), tf->GetWLength(), tf->GetWString().c_str() );
            }

            MediaFrame::RtpPacketizationInfo & info = f->GetRtpPacketizationInfo();
            for( MediaFrame::RtpPacketizationInfo::iterator it = info.begin(); it != info.end(); ++it )
            {
                nRtp++;
                DWORD sz = (*it)->GetSize() + (*it)->GetPrefixLen();
                if( sz > maxPayload ) maxPayload = sz;
            }
        }

        if( errcode < 0 ) { printf( "  erreur errcode=%d\n", errcode ); break; }
        if( waittime > 0 ) usleep( waittime * 1000 );
    }

    double elapsed = nowSec() - t0;
    printf( "trames=%d (video=%d intra=%d, audio=%d, texte=%d)\n", nFrames, nVideo, nIntra, nAudio, nText );
    printf( "paquets RTP=%d  payload max=%u (<=1400 attendu)\n", nRtp, maxPayload );
    printf( "1re trame intra length=%u octets (doit inclure SPS/PPS)\n", firstIntraLen );
    printf( "temps réel écoulé=%.2f s  (durée fichier=%.2f s)\n", elapsed, reader.GetDuration() );

    // -------- P4 : Seek / PreSeek --------
    printf( "\n== P4 : seek à mi-fichier ==\n" );
    QWORD target = (QWORD)( reader.GetDuration() * 1000.0 / 2.0 );
    QWORD pre    = reader.PreSeek( target );
    QWORD actual = reader.Seek( target );
    printf( "demandé=%llu ms  PreSeek=%llu ms  Seek(actual)=%llu ms\n",
            (unsigned long long)target, (unsigned long long)pre, (unsigned long long)actual );

    // Lit quelques trames après seek SANS cadencer (juste pour valider le contenu)
    int post = 0; bool firstIsIntra = false;
    for( ; post < 10; )
    {
        int errcode = 0; unsigned long waittime = 0;
        MediaFrame * f = reader.GetNextFrame( errcode, waittime );
        if( errcode == -1 ) break;
        if( f )
        {
            if( post == 0 && f->GetType() == MediaFrame::Video )
                firstIsIntra = ((VideoFrame*)f)->IsIntra();
            post++;
        }
        if( errcode < 0 ) break;
        if( waittime > 0 ) usleep( waittime * 1000 );
    }
    printf( "après seek : %d trames lues, 1re trame intra=%d (attendu 1 : keyframe)\n",
            post, firstIsIntra );

    return 0;
}
