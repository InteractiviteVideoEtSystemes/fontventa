#include "medkit/astcpp.h"
#include "medkit/mp4reader.h"
#include "mp4track.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"

mp4reader::mp4reader( void *ctxdata, MP4FileHandle mp4 )
{
    this->mp4 = mp4;
    this->ctxdata = ctxdata;
    mediatracks[MP4_AUDIO_TRACK] = NULL;
    next[MP4_AUDIO_TRACK] = MP4_INVALID_TIMESTAMP;
    mediatracks[MP4_VIDEO_TRACK] = NULL;
    next[MP4_VIDEO_TRACK] = MP4_INVALID_TIMESTAMP;
    mediatracks[MP4_VIDEODOC_TRACK] = NULL;
    next[MP4_VIDEODOC_TRACK] = MP4_INVALID_TIMESTAMP;
    mediatracks[MP4_TEXT_TRACK] = NULL;
    next[MP4_TEXT_TRACK] = MP4_INVALID_TIMESTAMP;
    redenc = NULL;
    currentTs = 0;
    gettimeofday( &startPlaying, 0 );
    nextBOMorRepeat = MP4_INVALID_TIMESTAMP;
}


int mp4reader::OpenTrack( AudioCodec::Type outputCodecs[], unsigned int nbCodecs, AudioCodec::Type prefCodec, bool cantranscode )
{
    if( nbCodecs > 0 )
    {
        MP4TrackId hintId = -1;
        MP4TrackId trackId = -1;
        MP4TrackId lastHintMatch = MP4_INVALID_TRACK_ID;
        MP4TrackId lastTrackMatch = MP4_INVALID_TRACK_ID;
        AudioCodec::Type lastCodecMatch;
        AudioCodec::Type c;

        int idxTrack = 0;

        if( mediatracks[MP4_AUDIO_TRACK] != NULL )
        {
            Error( "Audio track is already open.\n" );
            return 0;
        }

        hintId = MP4FindTrackId( mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0 );
        while( hintId != MP4_INVALID_TRACK_ID )
        {
            const char *nm = MP4GetTrackMediaDataName( mp4, hintId );
            //Debug("found hint track %d (%s)\n", hintId,nm?nm: "null");

            /* Get associated track */
            trackId = MP4GetHintTrackReferenceTrackId( mp4, hintId );

            /* Check it's good */
            if( trackId != MP4_INVALID_TRACK_ID )
            {
                /* Get type */
                const char *tt = MP4GetTrackType( mp4, trackId );

                if( tt != NULL && strcmp( tt, MP4_AUDIO_TRACK_TYPE ) == 0 )
                {
                    char *name;

                    MP4GetHintTrackRtpPayload( mp4, hintId, &name, NULL, NULL, NULL );

                    if( name == NULL )
                    {
                        c = AudioCodec::AMR;
                        name = (char *)"AMR";
                    }
                    else
                    {
                        if( !AudioCodec::GetCodecFor( name, c ) )
                        {
                            Log( "Unsupported audio codec %s for hint track ID %d.\n", name, hintId );
                            MP4Free( name );
                            name = NULL;
                            goto audio_track_loop1;
                        }
                    }

                    Debug( "found hinted track %d (%s)\n", trackId, name ? name : "null" );
                    if( c == prefCodec )
                    {
                        // This is the preffered codec !
                        // use it and stop here

                        lastTrackMatch = trackId;
                        lastHintMatch = hintId;
                        lastCodecMatch = c;
                        if( c != AudioCodec::AMR ) MP4Free( name );
                        name = NULL;
                        break;
                    }
                    else if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        for( int i = 0; i < nbCodecs; i++ )
                        {
                            if( outputCodecs[i] == c )
                            {
                                lastTrackMatch = trackId;
                                lastHintMatch = hintId;
                                lastCodecMatch = c;
                            }
                        }
                    }

                    if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        Log( "Codec %s is not compatible with requested output codecs.\n", name );
                    }
                    if( c != AudioCodec::AMR ) MP4Free( name );
                    name = NULL;
                }
            }
            else
            {
                Log( "No media track associated with hint track ID %d.\n", hintId );
            }

        audio_track_loop1:
            idxTrack++;
            hintId = MP4FindTrackId( mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0 );
        }

        if( lastTrackMatch == MP4_INVALID_TRACK_ID )
        {
            Log( "Try reopening audio track without hint.\n" );
            idxTrack = 0;
            trackId = MP4FindTrackId( mp4, idxTrack, MP4_AUDIO_TRACK_TYPE, 0 );
            while( trackId != MP4_INVALID_TRACK_ID )
            {
                const char *nm = MP4GetTrackMediaDataName( mp4, trackId );
                Debug( "found media track %d (%s)\n", trackId, nm ? nm : "null" );

                /* Get type */
                const char *tt = MP4GetTrackType( mp4, trackId );

                if( tt != NULL && strcmp( tt, MP4_AUDIO_TRACK_TYPE ) == 0 )
                {
                    const char *name = MP4GetTrackMediaDataName( mp4, trackId );

                    if( name == NULL )
                    {
                        c = AudioCodec::AMR;
                    }
                    else
                    {
                        if( !AudioCodec::GetCodecFor( name, c ) )
                        {
                            Log( "Unsupported audio codec %d for hint track ID %d.\n", name, hintId );
                            goto audio_track_loop2;
                        }
                    }

                    if( c == prefCodec )
                    {
                        // This is the preffered codec !
                        // use it and stop here

                        lastTrackMatch = trackId;
                        lastHintMatch = hintId;
                        lastCodecMatch = c;
                        break;
                    }

                    if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        for( int i = 0; i < nbCodecs; i++ )
                        {
                            if( outputCodecs[i] == c )
                            {
                                lastTrackMatch = trackId;
                                lastHintMatch = hintId;
                                lastCodecMatch = c;
                            }
                        }
                    }

                    if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        Debug( "Codec %s is not compatible with requested output codecs.\n", name );
                    }
                }

            audio_track_loop2:
                idxTrack++;
                trackId = MP4FindTrackId( mp4, idxTrack, MP4_AUDIO_TRACK_TYPE, 0 );
            }
        }

        if( lastTrackMatch != MP4_INVALID_TRACK_ID )
        {
            mediatracks[MP4_AUDIO_TRACK] = new Mp4AudioTrack( mp4, lastTrackMatch, lastHintMatch, lastCodecMatch );
            next[MP4_AUDIO_TRACK] = mediatracks[MP4_AUDIO_TRACK]->GetNextFrameTime();
            Log( "Opened audio track ID %d.\n", lastTrackMatch );
            return 1;
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

int mp4reader::OpenTrack( VideoCodec::Type outputCodecs[], unsigned int nbCodecs, VideoCodec::Type prefCodec, bool cantranscode, bool secondary )
{
    if( nbCodecs > 0 )
    {
        MP4TrackId hintId = MP4_INVALID_TRACK_ID;
        MP4TrackId trackId = MP4_INVALID_TRACK_ID;
        MP4TrackId lastHintMatch = MP4_INVALID_TRACK_ID;
        MP4TrackId lastTrackMatch = MP4_INVALID_TRACK_ID;
        int idxTrack = 0;
        VideoCodec::Type c;

        if( mediatracks[MP4_VIDEO_TRACK] != NULL )
        {
            Error( "Video track is already open.\n" );
            return 0;
        }

        hintId = MP4FindTrackId( mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0 );
        while( hintId != MP4_INVALID_TRACK_ID )
        {
            const char *nm = MP4GetTrackMediaDataName( mp4, hintId );
            Debug( "found hint track %d (%s)\n", hintId, nm ? nm : "null" );

            /* Get associated track */
            trackId = MP4GetHintTrackReferenceTrackId( mp4, hintId );

            /* Check it's good */
            if( trackId != MP4_INVALID_TRACK_ID )
            {
                /* Get type */
                const char *tt = MP4GetTrackType( mp4, trackId );

                if( tt != NULL && strcmp( tt, MP4_VIDEO_TRACK_TYPE ) == 0 )
                {
                    char *name;

                    MP4GetHintTrackRtpPayload( mp4, hintId, &name, NULL, NULL, NULL );

                    if( name == NULL )
                    {
                        Log( "No video codec %d for hint track ID %d.\n", name, hintId );
                        goto video_track_loop;
                    }
                    else
                    {
                        if( !VideoCodec::GetCodecFor( name, c ) )
                        {
                            Log( "Unsupported video codec %d for hint track ID %d.\n", name, hintId );
                            MP4Free( name );
                            name = NULL;
                            goto video_track_loop;
                        }
                    }
                    Debug( "found hinted video track %d (%s)\n", trackId, name ? name : "null" );

                    if( c == prefCodec )
                    {
                        // This is the preffered codec !
                        // use it and stop here
                        Debug( "Video track %d matches preferred codec %s\n", trackId, VideoCodec::GetNameFor( c ) );
                        lastTrackMatch = trackId;
                        lastHintMatch = hintId;
                        MP4Free( name );
                        name = NULL;
                        break;
                    }

                    if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        for( int i = 0; i < nbCodecs; i++ )
                        {
                            if( outputCodecs[i] == c )
                            {
                                lastTrackMatch = trackId;
                                lastHintMatch = hintId;
                            }
                        }
                    }

                    if( lastTrackMatch == MP4_INVALID_TRACK_ID )
                    {
                        Debug( "Video codec %s is not compatible with requested output codecs.\n", name );
                    }
                    MP4Free( name );
                    name = NULL;
                }
            }
            else
            {
                Log( "No media track associated with hint track ID %d.\n", hintId );
            }

        video_track_loop:
            idxTrack++;
            hintId = MP4FindTrackId( mp4, idxTrack, MP4_HINT_TRACK_TYPE, 0 );
        }

        if( lastTrackMatch != MP4_INVALID_TRACK_ID )
        {
            if( secondary )
            {
                mediatracks[MP4_VIDEODOC_TRACK] = new Mp4VideoTrack( mp4, lastTrackMatch, lastHintMatch, c );
                next[MP4_VIDEODOC_TRACK] = mediatracks[MP4_VIDEODOC_TRACK]->GetNextFrameTime();
            }
            else
            {
                mediatracks[MP4_VIDEO_TRACK] = new Mp4VideoTrack( mp4, lastTrackMatch, lastHintMatch, c );
                next[MP4_VIDEO_TRACK] = mediatracks[MP4_VIDEO_TRACK]->GetNextFrameTime();
            }
            Log( "Opened video track ID %d hint track %d.\n", lastTrackMatch, lastHintMatch );
            return 1;
        }
        else
        {
            return 0;
        }
    }
    return 0;
}

int mp4reader::OpenTrack( TextCodec::Type c, BYTE pt, int rendering )
{
    if( mediatracks[MP4_TEXT_TRACK] != NULL )
    {
        Error( "Text track is already open.\n" );
        return 0;
    }

    if( c == TextCodec::T140RED )
    {
        redenc = new RTPRedundantEncoder( pt );
    }

    MP4TrackId textId = MP4FindTrackId( mp4, 0, MP4_SUBTITLE_TRACK_TYPE, 0 );

    if( textId != MP4_INVALID_TRACK_ID )
    {
        mediatracks[MP4_TEXT_TRACK] = new Mp4TextTrack( mp4, textId );
        next[MP4_TEXT_TRACK] = mediatracks[MP4_TEXT_TRACK]->GetNextFrameTime();
        Log( "Opened text track ID %d.\n", textId );
        if( next[MP4_TEXT_TRACK] == MP4_INVALID_TIMESTAMP )
        {
            Error( "No valid subtitle sample !\n" );
        }
        return 1;
    }
    else
    {
        Debug( "No text track in this file.\n" );
        mediatracks[MP4_TEXT_TRACK] = NULL;
        next[MP4_TEXT_TRACK] = MP4_INVALID_TIMESTAMP;
        return -1;
    }
}

bool mp4reader::Eof( void )
{
    if( mediatracks[MP4_AUDIO_TRACK] && next[MP4_AUDIO_TRACK] != MP4_INVALID_TIMESTAMP )
        return false;

    if( mediatracks[MP4_VIDEO_TRACK] && next[MP4_VIDEO_TRACK] != MP4_INVALID_TIMESTAMP )
        return false;

    if( mediatracks[MP4_TEXT_TRACK] && next[MP4_TEXT_TRACK] != MP4_INVALID_TIMESTAMP )
        return false;

    return true;
}

bool mp4reader::GetCodec( AudioCodec::Type &codec ) const
{
    if( mediatracks[MP4_AUDIO_TRACK] )
    {
        Mp4AudioTrack *audiot = (Mp4AudioTrack *)mediatracks[MP4_AUDIO_TRACK];

        codec = audiot->GetCodec();
        return true;
    }
    return false;
}

bool mp4reader::GetVideoCodec( VideoCodec::Type &codec ) const
{
    if( mediatracks[MP4_VIDEO_TRACK] )
    {
        Mp4VideoTrack *videot = (Mp4VideoTrack *)mediatracks[MP4_VIDEO_TRACK];

        codec = videot->GetCodec();
        return true;
    }
    return false;
}

int mp4reader::Rewind()
{
    gettimeofday( &startPlaying, 0 );
    currentTs = 0;
    for( int i = 0; i < 4; i++ )
    {
        if( mediatracks[i] )
        {
            mediatracks[i]->Reset();
            next[i] = mediatracks[i]->GetNextFrameTime();
        }
        else
        {
            next[i] = MP4_INVALID_TIMESTAMP;
        }
    }
    return 1;
}

bool mp4reader::GetNextTrackAndTs( int &trackId, QWORD &ts )
{
    ts = MP4_INVALID_TIMESTAMP;

    for( int i = 0; i < 4; i++ )
    {
        if( mediatracks[i] && next[i] < ts )
        {
            ts = next[i];
            trackId = i;
        }
    }

    return (ts != MP4_INVALID_TIMESTAMP);
}

MediaFrame *mp4reader::GetNextFrame( int &errcode, unsigned long &waittime )
{
    //timeval tv ;
    //timespec ts;
    MediaFrame *f2 = NULL;
    QWORD t = 0;
    int trackId;

    //DWORD now = getUpdDifTime(&startPlaying);
    DWORD now = getDifTime( &startPlaying ) / 1000;

    if( !Eof() )
    {

        if( !GetNextTrackAndTs( trackId, t ) )
        {
            errcode = -2;
            return NULL;
        }

        // Handle RTT rentransmission and regular BOM sending in idle phase
        // TODO:
        if( redenc && nextBOMorRepeat != MP4_INVALID_TIMESTAMP )
        {
            if( now >= nextBOMorRepeat && now < next[MP4_TEXT_TRACK] )
            {
                if( redenc->IsIdle() )
                    redenc->EncodeBOM();
                else
                    redenc->Encode( NULL );

                if( redenc->IsIdle() )
                    nextBOMorRepeat = now + 5000;
                else
                    nextBOMorRepeat = now + 100;

                return redenc->GetRedundantPayload();
            }
        }

        if( now < t )
        {
            // we need to wait
            waittime = t - now;
            errcode = 0;
            //Debug("mp4play: case  now < t. waittime=%lu\n", waittime);
            return NULL;
        }

        currentTs = t;

        if( mediatracks[trackId] == NULL )
        {
            next[trackId] = MP4_INVALID_TIMESTAMP;
            errcode = -3;
            f2 = NULL;
        }
        else
        {
            f2 = (MediaFrame *)mediatracks[trackId]->ReadFrame();
            if( f2 == NULL )
            {
                errcode = -4;
                return NULL;
            }

            //Debug("mp4play: got frame from media %d\n", trackId);
            next[trackId] = mediatracks[trackId]->GetNextFrameTime();

            if( trackId == MP4_TEXT_TRACK )
            {
                // Special case for text
                if( redenc )
                {
                    DWORD ts = f2->GetTimeStamp();
                    redenc->Encode( f2 );
                    f2 = redenc->GetRedundantPayload();
                    f2->SetTimestamp( ts );
                    nextBOMorRepeat = now + 100;
                }

                errcode = 1;
            }
            else if( !f2->HasRtpPacketizationInfo() )
            {
                if( !f2->Packetize( 1400 ) )
                {
                    errcode = -5;
                }
                else
                {
                    errcode = 1;
                }
            }
            else
            {
                errcode = 1;
            }
        }

        if( GetNextTrackAndTs( trackId, t ) )
        {
            if( now >= t )
            {
                // we do not need to wait
                //Debug("no need to wait after frame is received f2=%p, now=%lld, ts =%lld\n", f2, now, t);
                waittime = 0;
            }
            else
            {
                // we need to wait
                waittime = t - now;

                // If RTT is used
                if( redenc )
                {
                    if( redenc->IsIdle() )
                    {
                        if( waittime > 5000 ) waittime = 5000;
                    }
                    else
                    {
                        if( waittime > 100 ) waittime = 100;
                    }
                }
            }
        }
        else
        {
            //failed to get next TS, probably end of file
            waittime = 0;
        }
    }
    else
    {
        //mp4Play: eof
        errcode = -1;
    }
    return f2;
}

mp4reader::~mp4reader()
{
    for( int i = 0; i < MP4_TEXT_TRACK + 1; i++ )
    {
        if( mediatracks[i] ) delete mediatracks[i];
    }

    if( redenc ) delete redenc;
}

// ---------------------------------------------------------------------------
// Groupe 1 — Métadonnées
// ---------------------------------------------------------------------------

double mp4reader::GetDuration()
{
    return (double)MP4GetDuration( mp4 ) / MP4GetTimeScale( mp4 );
}

DWORD mp4reader::GetVideoWidth()
{
    if( !mediatracks[MP4_VIDEO_TRACK] ) return 0;
    return MP4GetTrackVideoWidth( mp4, mediatracks[MP4_VIDEO_TRACK]->GetTrackId() );
}

DWORD mp4reader::GetVideoHeight()
{
    if( !mediatracks[MP4_VIDEO_TRACK] ) return 0;
    return MP4GetTrackVideoHeight( mp4, mediatracks[MP4_VIDEO_TRACK]->GetTrackId() );
}

DWORD mp4reader::GetVideoBitrate()
{
    if( !mediatracks[MP4_VIDEO_TRACK] ) return 0;
    return MP4GetTrackBitRate( mp4, mediatracks[MP4_VIDEO_TRACK]->GetTrackId() );
}

double mp4reader::GetVideoFramerate()
{
    if( !mediatracks[MP4_VIDEO_TRACK] ) return 0.0;
    return MP4GetTrackVideoFrameRate( mp4, mediatracks[MP4_VIDEO_TRACK]->GetTrackId() );
}

AVCDescriptor* mp4reader::GetAVCDescriptor()
{
    if( !mediatracks[MP4_VIDEO_TRACK] ) return NULL;

    Mp4VideoTrack *vt = (Mp4VideoTrack*)mediatracks[MP4_VIDEO_TRACK];
    if( vt->GetCodec() != VideoCodec::H264 ) return NULL;

    MP4TrackId trackId = vt->GetTrackId();

    uint8_t **sequenceHeader = NULL, **pictureHeader = NULL;
    uint32_t *sequenceHeaderSize = NULL, *pictureHeaderSize = NULL;
    uint32_t naluLen = 4;

    MP4GetTrackH264LengthSize( mp4, trackId, &naluLen );
    MP4GetTrackH264SeqPictHeaders( mp4, trackId,
        &sequenceHeader, &sequenceHeaderSize,
        &pictureHeader,  &pictureHeaderSize );

    AVCDescriptor *desc = new AVCDescriptor();
    desc->SetConfigurationVersion( 0x01 );
    desc->SetNALUnitLength( naluLen > 0 ? (BYTE)(naluLen - 1) : 3 );

    if( sequenceHeader && sequenceHeaderSize )
    {
        for( uint32_t i = 0; sequenceHeader[i] && sequenceHeaderSize[i]; i++ )
        {
            if( sequenceHeaderSize[i] >= 4 )
            {
                desc->SetAVCProfileIndication( sequenceHeader[i][1] );
                desc->SetProfileCompatibility( sequenceHeader[i][2] );
                desc->SetAVCLevelIndication(   sequenceHeader[i][3] );
            }
            desc->AddSequenceParameterSet( sequenceHeader[i], sequenceHeaderSize[i] );
            free( sequenceHeader[i] );
        }
    }
    if( pictureHeader && pictureHeaderSize )
    {
        for( uint32_t i = 0; pictureHeader[i] && pictureHeaderSize[i]; i++ )
        {
            desc->AddPictureParameterSet( pictureHeader[i], pictureHeaderSize[i] );
            free( pictureHeader[i] );
        }
    }

    if( pictureHeader )     MP4Free( pictureHeader );
    if( pictureHeaderSize ) MP4Free( pictureHeaderSize );
    if( sequenceHeader )    MP4Free( sequenceHeader );
    if( sequenceHeaderSize) MP4Free( sequenceHeaderSize );

    return desc;
}

// ---------------------------------------------------------------------------
// Groupe 2 — Seek / position
// ---------------------------------------------------------------------------

QWORD mp4reader::Seek( QWORD timeMs )
{
    QWORD actualTime = MP4_INVALID_TIMESTAMP;

    // La vidéo pilote le point de seek (sync frame la plus proche)
    if( mediatracks[MP4_VIDEO_TRACK] )
    {
        actualTime = mediatracks[MP4_VIDEO_TRACK]->SeekNearestSyncFrame( timeMs );
        next[MP4_VIDEO_TRACK] = mediatracks[MP4_VIDEO_TRACK]->GetNextFrameTime();
    }
    if( mediatracks[MP4_VIDEODOC_TRACK] )
    {
        QWORD target = (actualTime != MP4_INVALID_TIMESTAMP) ? actualTime : timeMs;
        mediatracks[MP4_VIDEODOC_TRACK]->Seek( target );
        next[MP4_VIDEODOC_TRACK] = mediatracks[MP4_VIDEODOC_TRACK]->GetNextFrameTime();
    }
    if( mediatracks[MP4_AUDIO_TRACK] )
    {
        QWORD target = (actualTime != MP4_INVALID_TIMESTAMP) ? actualTime : timeMs;
        QWORD audioTime = mediatracks[MP4_AUDIO_TRACK]->Seek( target );
        if( actualTime == MP4_INVALID_TIMESTAMP ) actualTime = audioTime;
        next[MP4_AUDIO_TRACK] = mediatracks[MP4_AUDIO_TRACK]->GetNextFrameTime();
    }
    if( mediatracks[MP4_TEXT_TRACK] )
    {
        QWORD target = (actualTime != MP4_INVALID_TIMESTAMP) ? actualTime : timeMs;
        mediatracks[MP4_TEXT_TRACK]->Seek( target );
        next[MP4_TEXT_TRACK] = mediatracks[MP4_TEXT_TRACK]->GetNextFrameTime();
    }

    nextBOMorRepeat = MP4_INVALID_TIMESTAMP;
    currentTs = (actualTime != MP4_INVALID_TIMESTAMP) ? actualTime : 0;

    // Recale l'horloge murale pour que GetNextFrame ne génère pas d'attente initiale
    gettimeofday( &startPlaying, 0 );
    if( actualTime > 0 )
    {
        long sec  = (long)(actualTime / 1000);
        long usec = (long)(actualTime % 1000) * 1000;
        startPlaying.tv_sec -= sec;
        if( startPlaying.tv_usec < usec )
        {
            startPlaying.tv_sec--;
            startPlaying.tv_usec += 1000000 - usec;
        }
        else
        {
            startPlaying.tv_usec -= usec;
        }
    }

    return actualTime;
}

QWORD mp4reader::PreSeek( QWORD timeMs )
{
    if( !mediatracks[MP4_VIDEO_TRACK] )
        return timeMs;
    QWORD snap = mediatracks[MP4_VIDEO_TRACK]->SearchNearestSyncFrame( timeMs );
    return (snap != MP4_INVALID_TIMESTAMP) ? snap : timeMs;
}
