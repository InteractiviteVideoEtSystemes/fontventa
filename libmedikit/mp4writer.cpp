#include "medkit/astcpp.h"
#include "medkit/mp4writer.h"
#include "mp4track.h"
#include "medkit/picturestreamer.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/text2subtitle.h"
#include "medkit/avcdescriptor.h"
#include "h264/h264depacketizer.h"
#include "h264/h264.h"

// Cadence du prologue vidéo : une trame noire toutes les 40 ms (25 im/s).
#define PROLOGUE_FRAME_MS   40
#define PROLOGUE_FRAME_TS   ( PROLOGUE_FRAME_MS * 90 )   /* horloge 90 kHz */

/*
 * Dimensions de l'image et profile-level-id, lus dans le SPS porté par la trame.
 *
 * Le dépacketiseur H.264 ne renseigne pas VideoFrame::width/height : sans cette
 * lecture, le prologue encoderait du 640x480 arbitraire. Or son SPS est le
 * premier écrit sur la piste, donc c'est lui qui alimente l'avcC -- une taille
 * fausse y resterait déclarée pour tout le fichier.
 *
 * Même raisonnement pour profile_idc/contraintes/level_idc : à la relecture, les
 * trames noires et la vidéo réelle se suivent dans un seul flux. Deux SPS de
 * niveaux différents y cohabiteraient (constaté : 42801F pour le prologue contre
 * 42c016 négocié et effectivement utilisé par le pair), et un décodeur
 * initialisé sur le premier peut refuser la suite. `plid` reçoit les 3 octets
 * sous forme hexadécimale, format de la propriété h264.profile-level-id.
 */
static bool GetSizeFromSps( VideoFrame *f, DWORD &width, DWORD &height, std::string &plid )
{
    if( f->GetCodec() != VideoCodec::H264 ) return false;

    MediaFrame::RtpPacketizationInfo &rtpInfo = f->GetRtpPacketizationInfo();

    for( MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin(); it != rtpInfo.end(); it++ )
    {
        BYTE *data = f->GetData() + (*it)->GetPos();

        if( (*it)->GetSize() < 2 ) continue;
        if( (data[0] & 0x1F) != 0x07 ) continue;

        H264SeqParameterSet sps;

        try
        {
            if( !sps.Decode( data + 1, (*it)->GetSize() - 1 ) ) continue;
        }
        catch( std::exception &e )
        {
            continue;
        }

        if( sps.GetWidth() == 0 || sps.GetHeight() == 0 ) continue;

        width = sps.GetWidth();
        height = sps.GetHeight();

        // 3 octets bruts du SPS : profile_idc, contraintes+reserved, level_idc
        char hex[8];
        snprintf( hex, sizeof( hex ), "%02X%02X%02X", data[1], data[2], data[3] );
        plid = hex;

        return true;
    }
    return false;
}

mp4writer::mp4writer( void *ctxdata, MP4FileHandle mp4, bool waitVideo )
{
    this->ctxdata = ctxdata;
    this->mp4 = mp4;
    textSeqNo = 0xFFFF;
    videoSeqNo = 0xFFFF;
    vtc = NULL;
    this->waitVideo = waitVideo ? 1 : 0;
    Log( "mp4recorder: created with waitVideo %s.\n", waitVideo ? "enabled" : "disabled" );
    audioencoder = NULL;
    depak = NULL;
    SetParticipantName( "participant" );
    gettimeofday( &firstframets, NULL );
    gettimeofday( &lastfur, NULL );
    initialDelay = 0;
    // Délai de référence commun aux pistes, 0 = pas encore connu. N'était pas
    // initialisé : la piste texte pouvait être décalée d'une valeur aléatoire.
    videoDelay = 0;
    for( int i = 0; i < MP4_TEXT_TRACK + 1; i++ )
    {
        mediatracks[i] = NULL;
    }
    pcstream = NULL;
    waitNextVideoFrame = false;
    saveTxtInComment = true;
    addVideoPrologue = true;
}

const char *idxToMedia( int i )
{
    switch( i )
    {
        case 0:
            return "Audio";

        case 1:
            return "Video";

        case 2:
            return "VideoDoc";

        case 3:
            return "Text";

        default:
            return "Unknown";
    }
}

mp4writer::~mp4writer()
{
    const MP4Tags *tags = MP4TagsAlloc();

    MP4TagsSetEncodingTool( tags, "MP4Save asterisk application" );
    MP4TagsSetArtist( tags, partName );

    if( mediatracks[MP4_TEXT_TRACK] != NULL )
    {
        std::string texte;
        Mp4TextTrack *txttrack = (Mp4TextTrack *)mediatracks[MP4_TEXT_TRACK];

        txttrack->GetSavedTextForVm( texte );

        if( saveTxtInComment && texte.length() > 0 )
        {
            if( !MP4TagsSetComments( tags, texte.c_str() ) )
            {
                Error( "mp4recorder: Save text inside mp4 comment tag failed.\n" );
            }
        }
    }

    MP4TagsStore( tags, mp4 );
    MP4TagsFree( tags );

    for( int i = 0; i < MP4_TEXT_TRACK + 1; i++ )
    {
        if( mediatracks[i] ) delete mediatracks[i];
    }

    if( audioencoder ) delete audioencoder;
    if( depak ) delete depak;
    if( pcstream ) delete pcstream;
}

void mp4writer::DumpInfo()
{
    const char *media;
    for( int i = 0; i < MP4_TEXT_TRACK + 1; i++ )
    {
        if( mediatracks[i] )
        {
            Log( "%s track ID %d has %d samples.\n",
                idxToMedia( i ), mediatracks[i]->GetTrackId(),
                mediatracks[i]->GetSampleId() );
        }
    }
    Log( "-----------------\n" );
}


int mp4writer::AddTrack( AudioCodec::Type codec, DWORD samplerate, const char *trackName )
{
    if( mediatracks[MP4_AUDIO_TRACK] == NULL )
    {
        mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack( mp4, initialDelay );
        if( mediatracks[MP4_AUDIO_TRACK] != NULL )
        {
            mediatracks[MP4_AUDIO_TRACK]->Create( trackName, (int)codec, samplerate );
            return 1;
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

int mp4writer::AddTrack( VideoCodec::Type codec, DWORD width, DWORD height, DWORD bitrate, const char *trackName, bool secondary )
{
    int trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;

    if( mediatracks[trackidx] == NULL )
    {
        Mp4VideoTrack *vtr = new Mp4VideoTrack( mp4, initialDelay );
        mediatracks[trackidx] = vtr;
        if( mediatracks[trackidx] != NULL )
        {
            vtr->SetSize( width, height );
            vtr->Create( trackName, codec, bitrate );
            return 1;
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

int mp4writer::AddTrack( TextCodec::Type codec, const char *trackName, int textfile )
{
    if( mediatracks[MP4_TEXT_TRACK] == NULL )
    {
        mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack( mp4, textfile, initialDelay );
        if( mediatracks[MP4_TEXT_TRACK] != NULL )
        {
            char introsubtitle[200];

            mediatracks[MP4_TEXT_TRACK]->Create( trackName, codec, 1000 );
            sprintf( introsubtitle, "[%s]\n", trackName );
            TextFrame tf( false );

            tf.SetMedia( (const uint8_t *)introsubtitle, strlen( introsubtitle ) );

            mediatracks[MP4_TEXT_TRACK]->ProcessFrame( &tf );

            return 1;
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

int mp4writer::IsVideoStarted()
{
    Mp4VideoTrack *vt = (Mp4VideoTrack *)mediatracks[MP4_VIDEO_TRACK];
    if( vt != NULL )
    {
        if( waitVideo == 0 ) return 1;
        if( vt->IsVideoStarted() ) return 1;
        if( depak != NULL && depak->MayBeIntra() ) return 1;
        return 0;
    }
    return -1;
}

int mp4writer::ProcessFrame( const MediaFrame *f, bool secondary )
{
    int trackidx;

    switch( f->GetType() )
    {
        case MediaFrame::Audio:
            if( mediatracks[MP4_AUDIO_TRACK] == NULL )
            {
                /* auto create audio track if needed */
                AudioFrame *f2 = (AudioFrame *)f;
                AddTrack( f2->GetCodec(), f2->GetRate(), partName );
            }

            if( mediatracks[MP4_AUDIO_TRACK] )
            {
                if( waitVideo ) return 0;

                if( mediatracks[MP4_AUDIO_TRACK]->IsEmpty() )
                {

                    // adjust initial delay
                    //if ( mediatracks[MP4_VIDEO_TRACK] )
                    //{
                        // Synchronize with video
                        //mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( videoDelay );
                    //}
                    //else
                    //{
                    // no video
                    if( addVideoPrologue && videoDelay > 0 )
                    {
                        /* Exactement la référence posée par le prologue vidéo,
                         * pas une nouvelle mesure : le silence initial de l'audio
                         * et les trames noires doivent s'achever au même instant.
                         * (L'audio est de toute façon jeté tant que la vidéo n'a
                         * pas démarré, cf. `if (waitVideo) return 0;` ci-dessus,
                         * donc son contenu réel commence bien à cet instant.) */
                        Log( "Adding %lu ms of initial delay + video start for audio.\n",
                             (unsigned long)videoDelay );
                        mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( videoDelay );
                    }
                    else if( addVideoPrologue )
                    {
                        // Pas de vidéo (ou prologue non écrit) : mesure propre.
                        DWORD delay = initialDelay + (getDifTime( &firstframets ) / 1000);

                        Log( "Adding %u of initial delay for audio (pas de référence vidéo).\n", delay );
                        mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( delay );
                    }
                    else if( initialDelay > 0 )
                    {
                        Log( "Adding %u of initial delay for audio.\n", initialDelay );
                        mediatracks[MP4_AUDIO_TRACK]->SetInitialDelay( initialDelay );
                    }
                    //}
                }

                int ret = mediatracks[MP4_AUDIO_TRACK]->ProcessFrame( f );
                //Log("Audio: track duration %u, real duration %u.\n", mediatracks[MP4_AUDIO_TRACK]->GetRecordedDuration(),
                //    getDifTime(&firstframets)/1000);
                return ret;
            }
            else return -3;

            break;

        case MediaFrame::Video:
            trackidx = secondary ? MP4_VIDEODOC_TRACK : MP4_VIDEO_TRACK;
            if( mediatracks[trackidx] )
            {
                VideoFrame *f2 = (VideoFrame *)f;
                Mp4VideoTrack *tr = (Mp4VideoTrack *)mediatracks[trackidx];

                /* Délai écoulé depuis le début de l'enregistrement, lu UNE SEULE
                 * FOIS par trame. L'encodage du prologue coûte quelques dizaines
                 * de ms : une seconde lecture d'horloge après lui donnerait une
                 * référence différente pour l'audio, donc un décalage entre les
                 * pistes (37 ms constatés en production). */
                DWORD nowDelay = initialDelay + (getDifTime( &firstframets ) / 1000);

                if( tr->IsEmpty() && pcstream == NULL )
                {
                    // Première trame vidéo : on connaît enfin le délai écoulé
                    // depuis le début de l'enregistrement. Il faut le combler
                    // avec de vraies trames, sinon la durée atterrit sur le
                    // premier échantillon réel, qui reste alors affiché pendant
                    // tout le délai (première image figée à la relecture).
                    DWORD delay = nowDelay;

                    if( addVideoPrologue && delay >= PROLOGUE_FRAME_MS )
                    {
                        Properties properties;
                        DWORD      w = 0, h = 0;
                        std::string plid;

                        // Aligne taille ET profile-level-id du prologue sur la
                        // vidéo réelle : son SPS sera le premier de la piste.
                        if( !GetSizeFromSps( f2, w, h, plid ) )
                        {
                            w = f2->GetWidth();
                            h = f2->GetHeight();
                        }
                        if( w == 0 || h == 0 )
                        {
                            w = 640;
                            h = 480;
                            Log( "-mp4recorder: taille vidéo inconnue, prologue en %lux%lu.\n",
                                 (unsigned long)w, (unsigned long)h );
                        }
                        if( !plid.empty() )
                            properties.SetProperty( "h264.profile-level-id", plid.c_str() );

                        pcstream = new PictureStreamer();
                        if( !pcstream->SetCodec( tr->GetCodec(), properties )
                            || !pcstream->SetFrameRate( 1000 / PROLOGUE_FRAME_MS, 100, 50 ) )
                        {
                            Error( "-mp4recorder: prologue vidéo indisponible (codec %s), "
                                   "délai non comblé.\n",
                                   VideoCodec::GetNameFor( tr->GetCodec() ) );
                            delete pcstream;
                            pcstream = NULL;
                            // Ne pas retenter à chaque trame
                            addVideoPrologue = false;
                        }
                        else
                        {
                            pcstream->PaintBlackRectangle( w, h );

                            // Le prologue couvre la totalité du délai, initialDelay
                            // compris : la piste ne doit pas l'étirer une 2e fois
                            // sur son premier échantillon.
                            tr->SetInitialDelay( 0 );

                            unsigned int n = WriteVideoPrologue( tr, f2->GetTimeStamp(), delay );

                            // Référence de synchro pour les autres pistes : la
                            // vidéo réelle commence exactement à cet instant.
                            if( n > 0 ) videoDelay = delay;

                            Log( "-mp4recorder: prologue vidéo de %u trames noires (%lux%lu) "
                                 "pour %lu ms de délai.\n",
                                 n, (unsigned long)w, (unsigned long)h, (unsigned long)delay );
                        }
                    }
                    else if( initialDelay > 0 )
                    {
                        // Pas de prologue : seul le délai explicitement demandé par
                        // l'appelant est reporté (participant arrivé en cours de
                        // route). Le temps d'établissement de la vidéo n'est PAS
                        // étiré sur la première image. Appel idempotent.
                        Log( "Adding %lu ms of initial delay for video.\n", initialDelay );
                        tr->SetInitialDelay( initialDelay );
                    }
                }

                if( waitVideo > 0 && f2->IsIntra() )
                {
                    waitVideo--;
                    if( waitVideo == 0 )
                    {
                        /* Référence de synchro : l'instant où le contenu vidéo
                         * réel commence, donc celui de CETTE trame -- l'attente
                         * de l'IDR a pu durer, et les trames noires émises
                         * pendant ce temps ont fait avancer la piste d'autant.
                         * `nowDelay` a été lu à l'entrée de l'appel : s'il inclut
                         * le prologue écrit ci-dessus, c'est bien la même valeur
                         * que lui, sans le temps d'encodage. */
                        videoDelay = nowDelay;
                        Log( "-mp4recorder: video has started after %lu ms.\n",
                             (unsigned long)nowDelay );
                    }
                    else
                    {
                        Log( "-mp4recorder: skipping first I-frame on purpose.\n" );
                        // this return code shoudl cause client to send FIR
                        return -333;
                    }

                }

                if( waitVideo > 0 )
                {
                    /* pcstream peut être NULL alors que le prologue est demandé :
                     * délai initial inférieur à une période, ou échec de création
                     * de l'encodeur. Le déréférencer ici plantait. */
                    if( addVideoPrologue && pcstream != NULL )
                    {
                        // We are still waiting for video
                        // Replace P-Frames with black frames
                        VideoFrame *f3 = pcstream->Stream( false );
                        if( f3 != NULL )
                        {
                            /* Pas de détour par `depak` : sa trame interne est
                             * celle que l'appelant vient de nous passer (f2), la
                             * réutiliser ici la détruirait. Et c'est inutile,
                             * l'encodeur rendant déjà de l'AVCC. */
                            f3->SetTimestamp( f2->GetTimeStamp() );
                            tr->ProcessFrame( f3 );
                        }
                    }

                    if( (getDifTime( &lastfur ) / 1000) > 2000 )
                    {
                        gettimeofday( &lastfur, NULL );
                        Debug( "mp4recorder: still no I frame. Requesting it again.\n" );
                        return -333;
                    }

                    return 0;
                }

                if( f->GetTimeStamp() == 0 ) Log( "mp4recorder: incorrect video timestamp = 0. Check asterisk version.\n" );

                // TS drift - compensate - disabled for now
                DWORD realDuration = getDifTime( &firstframets ) / 1000;

                /* if ( realDuration > tr->GetRecordedDuration()
                     &&
                     realDuration - tr->GetRecordedDuration() > 1000 )
                {
                     videoDelay += 10;
                }
                */

                //Log("Video: track duration %u, real duration %u.\n",tr->GetRecordedDuration(),
                //    getDifTime(&firstframets)/1000);
                int ret = tr->ProcessFrame( f2 );
                return ret;
            }
            else
            {
                return -3;
            }
            break;

        case MediaFrame::Text:
            if( mediatracks[MP4_TEXT_TRACK] == NULL )
            {
                /* auto create text track if needed.
                 * textfile=-1 : pas de fichier texte annexe -- 0 serait pris
                 * pour un fd valide (stdin !) par les gardes `textfile >= 0`
                 * de Mp4TextTrack (onNewLine y ecrivait les sous-titres et
                 * GetSavedTextForVm bloquait dans read(0) a la fermeture). */
                AddTrack( TextCodec::T140, &partName[0], -1 );
            }

            if( mediatracks[MP4_TEXT_TRACK] )
            {
                if( waitVideo ) return 0;

                if( mediatracks[MP4_TEXT_TRACK]->IsEmpty() )
                {
                    // adjust initial delay
                    if( mediatracks[MP4_VIDEO_TRACK] )
                    {
                        // Synchronize with video
                        mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( videoDelay );
                    }
                    else
                    {
                        // no video
                        mediatracks[MP4_TEXT_TRACK]->SetInitialDelay( initialDelay + (getDifTime( &firstframets ) / 1000) );
                    }
                }

                return mediatracks[MP4_TEXT_TRACK]->ProcessFrame( f );
            }
            return -3;

        default:
            break;
    }
    return 0;
}

/*
 * Comble le délai précédant la première trame vidéo réelle avec des trames
 * noires encodées, plutôt que d'étirer la durée de cette première trame.
 *
 * Les horodatages courent VERS L'AVANT depuis `firstTs`, et la vidéo réelle est
 * ensuite décalée d'autant (SetTimestampOffset). Un calcul à rebours depuis
 * `firstTs` serait plus direct mais est impossible ici : côté Asterisk les ts
 * vidéo sont relatifs à la première trame, donc `firstTs` vaut 0 et il n'existe
 * aucune place avant lui.
 *
 * La piste n'écrit un échantillon qu'à l'arrivée du suivant : la dernière trame
 * noire est écrite quand la trame réelle arrive, avec pour durée l'écart créé
 * par le décalage. Ce décalage vaut le délai EXACT (pas un multiple de la
 * période) : la dernière trame noire absorbe le reste de la division et le
 * prologue couvre `delayMs` à la milliseconde, sans quoi jusqu'à
 * PROLOGUE_FRAME_MS-1 ms de désynchronisation avec l'audio subsisteraient.
 */
unsigned int mp4writer::WriteVideoPrologue( Mp4VideoTrack *tr, DWORD firstTs, DWORD delayMs )
{
    unsigned int nframes = delayMs / PROLOGUE_FRAME_MS;
    unsigned int written = 0;

    for( unsigned int i = 0; i < nframes; i++ )
    {
        // La 1re trame doit être une intra : Mp4VideoTrack ignore tout ce qui
        // précède la première image clé. Elle porte aussi les SPS/PPS qui
        // initialisent l'avcC de la piste.
        VideoFrame *bf = pcstream->Stream( i == 0 );
        if( bf == NULL ) break;

        // L'encodeur rend déjà des NALs préfixées par leur taille (AVCC), le
        // format attendu par MP4WriteSample : aucune conversion à faire.
        bf->SetTimestamp( firstTs + i * PROLOGUE_FRAME_TS );
        if( tr->ProcessFrame( bf ) < 0 ) break;
        written++;
    }

    // Rien d'écrit (échec d'encodage) : pas de décalage, sinon la vidéo réelle
    // serait reportée derrière un prologue inexistant.
    if( written > 0 ) tr->SetTimestampOffset( delayMs * 90 );

    return written;
}

void  mp4writer::SetInitialDelay( unsigned long delay )
{
    initialDelay = delay;

    for( int i = 0; i < (MP4_TEXT_TRACK + 1); i++ )
    {
        if( mediatracks[i] )  mediatracks[i]->SetInitialDelay( delay );
    }

}

void mp4writer::Flush()
{
    if( mediatracks[MP4_VIDEO_TRACK] )
    {
        Mp4VideoTrack *tr = (Mp4VideoTrack *)mediatracks[MP4_VIDEO_TRACK];
        tr->WriteLastFrame();
    }

    if( mediatracks[MP4_TEXT_TRACK] )
    {
        /* Tient le dernier sous-titre jusqu'a la fin de l'enregistrement : la
         * piste texte n'ecrit un echantillon qu'a l'arrivee du suivant, donc
         * sans cette purge le texte disparaissait a la derniere frappe. */
        Mp4TextTrack *tr = (Mp4TextTrack *)mediatracks[MP4_TEXT_TRACK];
        tr->FlushSubtitle( initialDelay + ( getDifTime( &firstframets ) / 1000 ) );
    }
}

/* ---- callbeck used for video transcoding --- */

void Mp4RecoderVideoCb( void *ctxdata, int outputcodec, const char *output, size_t outputlen )
{
    mp4writer *r2 = (mp4writer *)ctxdata;
    VideoFrame vf( (VideoCodec::Type)outputcodec, 2000, false );

    if( r2 )
    {
        vf.SetMedia( (uint8_t *)output, outputlen );
    // add timestamp
        r2->ProcessFrame( &vf );
    }
}
