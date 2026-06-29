#include "medkit/astcpp.h"
#include "medkit/mp4recorder.h"
#include "mp4track.h"
#include "medkit/picturestreamer.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"
#include "h264/h264depacketizer.h"

mp4recorder::mp4recorder( void *ctxdata, MP4FileHandle mp4, bool waitVideo )
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

mp4recorder::~mp4recorder()
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

void mp4recorder::DumpInfo()
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


int mp4recorder::AddTrack( AudioCodec::Type codec, DWORD samplerate, const char *trackName )
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

int mp4recorder::AddTrack( VideoCodec::Type codec, DWORD width, DWORD height, DWORD bitrate, const char *trackName, bool secondary )
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

int mp4recorder::AddTrack( TextCodec::Type codec, const char *trackName, int textfile )
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

int mp4recorder::IsVideoStarted()
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

int mp4recorder::ProcessFrame( const MediaFrame *f, bool secondary )
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
                    if( addVideoPrologue )
                    {
                        DWORD delay = initialDelay + (getDifTime( &firstframets ) / 1000);

                        Log( "Adding %u of initial delay + video start for audio.\n", delay );
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

                if( tr->IsEmpty() )
                {
                    Properties properties;

                    if( pcstream == NULL )
                    {
                        DWORD delay = initialDelay + (getDifTime( &firstframets ) / 1000);
                        Log( "-mp4recorder: Initializing video prologue.\n" );
                        Log( "Adding %u of initial delay for video.\n", delay );
                        pcstream = new  PictureStreamer();
                        pcstream->SetCodec( tr->GetCodec(), properties );
                        pcstream->SetFrameRate( 25, 100, 50 );
                        pcstream->PaintBlackRectangle( 640, 480 );
                        tr->SetInitialDelay( delay );
                    }
                }

                if( waitVideo > 0 && f2->IsIntra() )
                {
                    waitVideo--;
                    if( waitVideo == 0 )
                    {
                        videoDelay = initialDelay + (getDifTime( &firstframets ) / 1000);
                        Log( "-mp4recorder: video has started after %lu ms.\n", getDifTime( &firstframets ) / 1000 );
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
                    if( addVideoPrologue )
                    {
                        // We are still waiting for video
                        // Replace P-Frames with black frames
                        VideoFrame *f3 = pcstream->Stream( false );
                        if( f3 != NULL )
                        {
                            // depaketize f3
                            DWORD ts = f2->GetTimeStamp();
                            MediaFrame *f4;

                            // Specific H.264. We would need to do it in the video frame class directly to remain multi codecs ...
                            depak->ResetFrame();

                            for( MediaFrame::RtpPacketizationInfo::iterator it = f3->GetRtpPacketizationInfo().begin();
                                it != f3->GetRtpPacketizationInfo().end();
                                it++ )

                            {
                                f4 = depak->AddPayload( f3->GetData() + (*it)->GetPos(), (*it)->GetSize(), (*it)->IsMark() );
                            }

                            if( f4 )
                            {
                                f4->SetTimestamp( ts );
                                tr->ProcessFrame( f4 );
                            }
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
                /* auto create text track if needed */
                const char *n = &partName[0];
                AddTrack( TextCodec::T140, n, 0 );
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
}

void  mp4recorder::SetInitialDelay( unsigned long delay )
{
    initialDelay = delay;

    for( int i = 0; i < (MP4_TEXT_TRACK + 1); i++ )
    {
        if( mediatracks[i] )  mediatracks[i]->SetInitialDelay( delay );
    }

}

void mp4recorder::Flush()
{
    if( mediatracks[MP4_VIDEO_TRACK] )
    {
        Mp4VideoTrack *tr = (Mp4VideoTrack *)mediatracks[MP4_VIDEO_TRACK];
        tr->WriteLastFrame();
    }
}

/* ---- callbeck used for video transcoding --- */

void Mp4RecoderVideoCb( void *ctxdata, int outputcodec, const char *output, size_t outputlen )
{
    mp4recorder *r2 = (mp4recorder *)ctxdata;
    VideoFrame vf( (VideoCodec::Type)outputcodec, 2000, false );

    if( r2 )
    {
        vf.SetMedia( (uint8_t *)output, outputlen );
    // add timestamp
        r2->ProcessFrame( &vf );
    }
}
