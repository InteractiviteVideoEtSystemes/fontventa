extern "C"
{
#include <asterisk/frame.h>
#include <asterisk/channel.h>
}
#include "medkit/astcpp.h"
#include "astmedkit/mp4format.h"
#include "medkit/picturestreamer.h"
#include "astmedkit/frameutils.h"
#include "medkit/red.h"
#include "medkit/log.h"
#include "medkit/textencoder.h"
#include "medkit/avcdescriptor.h"
#include "h264/h264depacketizer.h"

bool AstFormatToCodec( int format, AudioCodec::Type &codec )
{
    switch( ((unsigned int)format) & 0xFFFFFFFE )
    {
        case AST_FORMAT_ULAW:
            codec = AudioCodec::PCMU;
            break;

        case AST_FORMAT_ALAW:
            codec = AudioCodec::PCMA;
            break;

        case AST_FORMAT_AMRNB:
            codec = AudioCodec::AMR;
            break;

        default:
            Log( "AstFormatToCodec: unsupported ast_format %08lu.\n",
                ((unsigned int)format) & 0xFFFFFFFE );
            return false;
    }
    return true;
}

bool AstFormatToCodec( int format, VideoCodec::Type &codec )
{
    switch( ((unsigned int)format) & 0xFFFFFFFE )
    {
        case AST_FORMAT_H263:
            codec = VideoCodec::H263_1996;
            break;

        case AST_FORMAT_H263_PLUS:
            codec = VideoCodec::H263_1998;
            break;

        case AST_FORMAT_H264:
            codec = VideoCodec::H264;
            break;

        default:
            return false;
    }
    return true;
}

int AstMp4Recorder::ProcessFrame( struct ast_frame *f, bool secondary )
{
    if( f != NULL )
    {
        int ret;

        switch( f->frametype )
        {
            case AST_FRAME_VOICE:
            {
                AudioCodec::Type acodec = AudioCodec::PCMU;
                AudioFrame af( acodec, 8000, false );

                af.SetTimestamp( f->ts );

                if( f->subclass == AST_FORMAT_SLINEAR )
                {
                    // If audio received is SLINEAR - transcode
                    int outLen = sizeof( audioBuff );
                    acodec = AudioCodec::PCMU;

                    if( audioencoder == NULL )
                        audioencoder = AudioCodecFactory::CreateEncoder( AudioCodec::PCMU );

                    outLen = audioencoder->Encode( (SWORD *)AST_FRAME_GET_BUFFER( f ), f->datalen / 2,
                        audioBuff, outLen );
                    if( outLen > 0 )
                        af.SetMedia( audioBuff, outLen );
                    else
                        return 0;

                }
                else if( AstFormatToCodec( f->subclass, acodec ) )
                {
                    af.SetCodec( acodec );
                    af.SetMedia( AST_FRAME_GET_BUFFER( f ), f->datalen );
                }
                else
                {
                    /* unsupported codec */
                    return -4;
                }
                return ProcessFrame( &af );
            }

            case AST_FRAME_VIDEO:
            {
                VideoCodec::Type vcodec;
                int ret;
                bool ismark = (f->subclass & 0x01) != 0;
                int loss_detected = false;
                if( AstFormatToCodec( f->subclass, vcodec ) )
                {
                    if( videoSeqNo != 0xFFFF )
                    {
                        if( f->seqno != 0xFFFF )
                        {
                            if( f->seqno != videoSeqNo + 1 ) loss_detected = true;
                        }
                    }

                    if( loss_detected )
                    {
                        Log( "video packet lost detected seqno=%d, expected =%d\n", f->seqno, videoSeqNo + 1 );
                        waitNextVideoFrame = true;
                        ret = -333; // Ask for a FUR
                    }

                    videoSeqNo = f->seqno;

                    switch( vcodec )
                    {
                        case VideoCodec::H264:
                        {
                            MediaFrame *vfh264;
                            if( depak == NULL )
                            {
                                depak = new H264Depacketizer();
                            }

                            // Accumulate NALs into the same frame until mark
                            vfh264 = depak->AddPayload( AST_FRAME_GET_BUFFER( f ), f->datalen, ismark );

                            // Do the same in case of lost frame
                            if( ismark )
                            {
                                if( strcasecmp( f->src, "RTP" ) == 0 )
                                {
                                    //Log("H.264 - got mark. frame ts = %ld, timingsource=TS.\n", f->ts );
                                    vfh264->SetTimestamp( f->ts );
                                }
                                else
                                {
                                    //Log("H.264 - got mark. frame ts = %ld, timingsource=internal.\n", f->ts );

                                    vfh264->SetTimestamp( getDifTime( &firstframets ) / 1000 );
                                }

                                if( !waitNextVideoFrame )
                                {
                                    ret = ProcessFrame( vfh264 );
                                }
                                else
                                {
                                    Log( "H.264 - ignoring incomplete frame  ts = %ld.\n", f->ts );
                                }
                                depak->ResetFrame();
                                waitNextVideoFrame = false;
                                return ret;
                            }
                            else
                            {
                                // no mark ? will be processed later
                                return 1;
                            }
                        }
                        break;

                        default:
                        {
                            // TODO: Accumulate all ast_frame in a single VideoFrame and pass it to processing
                            //
                            VideoFrame vf( vcodec, f->datalen, false );
                            if( strcasecmp( f->src, "RTP" ) == 0 )
                                vf.SetTimestamp( f->ts );
                            else
                                vf.SetTimestamp( getDifTime( &firstframets ) / 1000 );

                            vf.SetMedia( AST_FRAME_GET_BUFFER( f ), f->datalen );
                            vf.AddRtpPacket( 0, f->datalen, NULL, 0, ismark );
                            ret = ProcessFrame( &vf );
                        }
                        break;

                    }

                    if( ret == -1 && vtc != NULL )
                    {
                        /* we need to transcode */
                        return VideoTranscoderProcessFrame( vtc, f );
                    }
                    return ret;
                }
                else
                {
                    return -4;
                }
            }
            break;

            case AST_FRAME_TEXT:
            {
                DWORD lost = 0, text_ts;
                TextCodec::Type tcodec;
                TextFrame tf( true );

                //If not first
                if( textSeqNo != 0xFFFF && f->seqno != 0xFFFF )
                    //Calculate losts
                    lost = f->seqno - textSeqNo - 1;

                //Update last sequence number
                textSeqNo = f->seqno;
                //Log("text frame seqno %d, lost %d\n", f->seqno, lost);

                // Generate timing INFO
                text_ts = getDifTime( &firstframets ) / 1000;

                if( (f->subclass & AST_FORMAT_TEXT_MASK) == AST_FORMAT_RED )
                {
                    // parse RED to recover lost packets
                    RTPRedundantPayload red( AST_FRAME_GET_BUFFER( f ), f->datalen );

                    if( lost > 0 && red.GetRedundantCount() > 0 )
                    {
                        Log( "text frame seqno %d, lost %d\n", f->seqno, lost );
                        if( lost > red.GetRedundantCount() )
                        {
                            /* cas ou l'on a perdu + de paquet de le niv de red. On ne fait rien */
                            lost = red.GetRedundantCount();
                        }

                        //Fore each recovered packet
                        for( int i = red.GetRedundantCount() - lost; i < red.GetRedundantCount(); i++ )
                        {
                            //Create frame from recovered data - check timestamps ...
                            tf.SetTimestamp( text_ts - red.GetRedundantTimestampOffset( i ) );
                            tf.SetMedia( red.GetRedundantPayloadData( i ), red.GetRedundantPayloadSize( i ) );
                            Log( "Recovering lost packet seqno %d from redundant data.", textSeqNo - i );
                            ProcessFrame( &tf );
                        }
                    }

                    /* char ttr[800];

                    strncpy( ttr, (const char *) red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
                    ttr[  red.GetPrimaryPayloadSize() ] = '\0';
                    Log("Primary data [%s] len %d.\n", ttr, red.GetPrimaryPayloadSize() ); */
                    tf.SetFrame( text_ts, red.GetPrimaryPayloadData(), red.GetPrimaryPayloadSize() );
                }
                else /* assume plain text */
                {
                    tf.SetFrame( text_ts, (const BYTE *)AST_FRAME_GET_BUFFER( f ), f->datalen );
                }

                return ProcessFrame( &tf );
            }
        }
    }
}

struct mp4rec *Mp4RecorderCreate( struct ast_channel *chan, MP4FileHandle mp4, bool waitVideo,
    const char *videoformat, const char *partName, int textfile )
{
    if( (chan->nativeformats & AST_FORMAT_VIDEO_MASK) == 0 )
    {
        waitVideo = 0;
        Log( "-mp4recorder: disable video waiting as char %s does not support video.\n",
            chan->name );
    }

    AstMp4Recorder *r = new AstMp4Recorder( chan, mp4, waitVideo );
    if( partName == NULL ) partName = chan->cid.cid_name ? chan->cid.cid_name : "unknown";
    if( r != NULL )
    {
#define MP4_SUPPORTED_AUDIO_FMT ( AST_FORMAT_ALAW | AST_FORMAT_AMRNB | AST_FORMAT_ULAW )

        int audio = (chan->nativeformats & AST_FORMAT_AUDIO_MASK);


        if( audio != 0 && (audio & MP4_SUPPORTED_AUDIO_FMT) == 0 )
        {
            Log( "-mp4recorder: no supported audio codec. Defaulting to U-Law.\n" );
            ast_set_read_format( chan, AST_FORMAT_ULAW );
        }

        r->SetParticipantName( partName );
        if( videoformat != NULL && strlen( videoformat ) > 0 && (chan->nativeformats & AST_FORMAT_VIDEO_MASK) != 0 )
        {
            // Hardcoded for now
            r->AddTrack( VideoCodec::H264, 640, 480, 256, partName, false );
        }

        if( chan->nativeformats & AST_FORMAT_TEXT_MASK )
            r->AddTrack( TextCodec::T140, partName, textfile );
    }

    return (struct mp4rec *)r;
}

void Mp4RecorderDestroy( struct mp4rec *r )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;
    r2->DumpInfo();
    delete r2;
}

int Mp4RecorderFrame( struct mp4rec *r, struct ast_frame *f )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;

    if( r2 )
    {
        int rez = r2->ProcessFrame( f );

        if( rez == 0 )
        {
            struct ast_channel *chan = (struct ast_channel *)r2->GetCtxData();
            if( chan != NULL && (chan->nativeformats & AST_FORMAT_VIDEO_MASK) == 0 )
            {
            /* AUdio frame ignored because we are waiting for video but no video on this chan
             * do not wait for video anymore and ... process the frame.
             */
                r2->SetWaitForVideo( false );
                Log( "-mp4recorder: disable video waiting as chan %s does not support video (process frame).\n",
                    chan->name );
                rez = r2->ProcessFrame( f );
            }
        }

        return rez;
    }
    else
        return -5;
}

int Mp4RecorderHasVideoStarted( struct mp4rec *r )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;

    return r2->IsVideoStarted();
}

void Mp4RecorderSetInitialDelay( struct mp4rec *r, unsigned long ms )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;

    r2->SetInitialDelay( ms );
}

void Mp4RecorderFlush( struct mp4rec *r )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;

    r2->Flush();
}

void Mp4RecorderEnableVideoPrologue( struct mp4rec *r, bool yesno )
{
    AstMp4Recorder *r2 = (AstMp4Recorder *)r;
    r2->EnableVideoPrologue( yesno );
}

struct mp4play *Mp4PlayerCreate( struct ast_channel *chan, MP4FileHandle mp4, bool transcodeVideo, int renderText )
{
    mp4player *p = new mp4player( chan, mp4 );

    if( p )
    {
        int haveAudio = chan->nativeformats & AST_FORMAT_AUDIO_MASK;
        int haveVideo = chan->nativeformats & AST_FORMAT_VIDEO_MASK;
        int haveText = chan->nativeformats & AST_FORMAT_TEXT_MASK;



        TextCodec::Type tc = (TextCodec::Type)-1;

        if( haveAudio )
        {
            AudioCodec::Type acodecList[10];
            unsigned int nbACodecs = 0;
            AudioCodec::Type ac = (AudioCodec::Type)-1;
            int ast_codec = 0;

            if( !AstFormatToCodecList( chan->writeformat, &ac ) )
            {
                delete p;
                Error( "mp4play: Failed to obtain preferred audio codec for chan %s\n", chan->name );
                Error( "mp4play: write format is %s (%x).\n", ast_getformatname( chan->writeformat ), chan->writeformat );
                return NULL;
            }

            // Add additionnal codecs to activate trancoding if nativeformat are not enough
            haveAudio |= AST_FORMAT_ALAW | AST_FORMAT_ULAW | AST_FORMAT_SLINEAR;

            nbACodecs = AstFormatToCodecList( haveAudio, acodecList, 10 );

            if( p->OpenTrack( acodecList, nbACodecs, ac, true ) < 0 )
            {
                Error( "mp4play: [%s] No suitable audio track found.\n", chan->name );
            }
            else
            {
                if( p->GetCodec( ac ) )
                {
                    CodecToAstFormat( ac, ast_codec );

                    Log( "mp4play: [%s] activating audio transcoding from %s.\n", chan->name, AudioCodec::GetNameFor( ac ) );
                    if( (chan->nativeformats & ast_codec) == 0 )
                    {
                        ast_set_write_format( chan, ast_codec );
                    }
                }
            }
        }

        if( haveVideo )
        {
            VideoCodec::Type vcodecList[3];
            unsigned int nbVCodecs = 0;
            VideoCodec::Type vc = (VideoCodec::Type)-1;

            if( !AstFormatToCodecList( chan->nativeformats, &vc ) )
            {
                delete p;
                Error( "mp4play: Failed to obtain preferred video codec for chan %s\n", chan->name );
                return NULL;
            }

            nbVCodecs = AstFormatToCodecList( chan->nativeformats, vcodecList, 3 );

            if( p->OpenTrack( vcodecList, nbVCodecs, vc, transcodeVideo, false ) < 0 )
            {
                Error( "mp4play: [%s]  No suitable video track found.\n", chan->name );
            }
        }

        if( haveText )
        {
            if( chan->nativeformats & AST_FORMAT_RED )
                tc = TextCodec::T140RED;
            else
                tc = TextCodec::T140;
            if( p->OpenTrack( tc, renderText, 1 ) < 0 )
            {
                Log( "mp4play: [%s]  No suitable text track found.\n", chan->name );
            }
        }
    }
    return (mp4play *)p;
}




int Mp4PlayerPlayNextFrame( struct ast_channel *chan, struct mp4play *p )
{
    mp4player *p2 = (mp4player *)p;
    unsigned long wait = 0;
    int ret = -1;

    while( wait == 0 )
    {
        MediaFrame *f = p2->GetNextFrame( ret, wait );

        if( f == NULL )
        {
            if( ret == 0 )
                return wait;
            else
            {
                if( ret != -1 ) Error( "GetNextFrame returned %d.\n", ret );
                return ret;
            }
        }

        if( f->HasRtpPacketizationInfo() )
        {
            MediaFrame::RtpPacketizationInfo &pinfo = f->GetRtpPacketizationInfo();
            struct ast_frame f2;

            for( MediaFrame::RtpPacketizationInfo::iterator it = pinfo.begin();
                it != pinfo.end();
                it++ )

            {
                bool  first = (it == pinfo.begin());

                if( !MediaFrameToAstFrame2( f, *it, first, f2, p2->buffer, sizeof( p2->buffer ) ) )
                {
                    return -5; /* incompatible codec read from MP4 file or unsupported media */
                }

                if( f->GetType() == MediaFrame::Audio )
                {
                    if( f2.subclass != chan->writeformat )
                    {
                        Log( "mp4play: activating audio transcoding.\n" );
                        ast_set_write_format( chan, f2.subclass );
                    }
                }

                if( ast_write( chan, &f2 ) < 0 )
                {
                    Error( "mp4play: failed to write frame with format %x.\n", f2.subclass );
                    return -6; /* write error */
                }

                //no need to free. Everything is static
                //ast_frfree(&f2);
            }
            f->ClearRTPPacketizationInfo();
        }
        else
        {
            Debug( "mp4play: Failed to get packetization info for frame.\n" );
        }
        //no need to free f it is recycled by the media track.
    }


    return (int)wait;

}

void Mp4PlayerDestroy( struct mp4play *p )
{
    mp4player *p2 = (mp4player *)p;
    if( p2 ) delete p2;
}
