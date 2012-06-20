#!/bin/ksh
# =============================================================================
# Unpublished Confidential Information of IVES Do not disclose.       
# Copyright (c)  IVES All Rights Reserved.                    
# ---------------------------------------------------------------------------
#
# COMPANY   IVES
#
# author   Philippe Verney
#
# file     $HeadURL$
#
# brief    Convert avi file to mp4 multitrack for asterisk
#               
# version  $Revision$
#
# date     $Date$
# 
# remarks  
# 
#--------------------------------------------------------------------------- 
# $Log$
# =============================================================================
#  -----1=0-------2=0-------3=0-------4=0-------5=0-------6=0-------7=0-------8

# =============================================================================
# Constant de convertion 
# =============================================================================
# video encoding bit rate
V_BITRATE=45000
V_SIZE="176x144"
V_SIZE_CIF="352x288"
FORMAT="-3gp/3g"
V_CODECIN=""
V_CODECOUT=""

V_SIZE_H263="qcif"
V_FPS_H263=7
V_BITRATE_H263=35000
V_BR_TOLERANCE_H263=10000
V_FFMPEG_OPTS_H263="-g 5 -flags loop -b_qfactor 0.8 -dct mmx -precmp rd -skipcmp rd -pre_dia_size 4 "

V_SIZE_H263_GOOD="cif"
V_FPS_H263_GOOD=15
V_BITRATE_H263_GOOD=200000
V_BR_TOLERANCE_H263_GOOD=100000
V_FFMPEG_OPTS_H263_GOOD="-g 8  -flags loop -b_qfactor 0.8 -dct mmx -precmp rd -skipcmp rd -pre_dia_size 4 "


V_SIZE_H264="cif"
V_FPS_H264=25
V_BITRATE_H264=340000
V_BR_TOLERANCE_H264=10000
V_FFMPEG_OPTS_H264="-g 250 -max_slice_size 1300 -level 13 -qmin 2 -qmax 35 -me_method hex "

# =============================================================================
# Constant de travail
# =============================================================================
BIN_PATH="/usr/bin"
INFO_FILE="/tmp/IVES_convert.inf"
EXIT_SUCCESS=0
EXIT_ERROR=1
#gestion des logs
LOG_FILE="/dev/null"

# =============================================================================
# Constant de travail
# =============================================================================
# flag de debug
integer debug=0
integer echo_on_stdout=1
integer mode_fast=0
# contenue des track
mimeType="other"
integer haveVideo=0
integer haveAudio=0
integer orgHaveVideo=0
integer firstCheck=0
# information audio
integer haveUlaw=0
integer idxUlawTrack=0
integer idxHintUlawTrack=0
integer haveAlaw=0
integer idxAlawTrack=0
integer idxHintAlawTrack=0
integer haveAmr=0
integer idxAmrTrack=0
integer idxHintAmrTrack=0
integer haveH263=0
integer idxH263Track=0
integer idxHintH263Track=0
integer h263_good=1
integer haveH264=0
integer idxH264Track=0
integer idxHintH264Track=0
# calcul duree video
integer duration=0
integer nb_frame=0
integer frame_rate=3 
# extraction du texte 
integer text=0
#gestion du flv
integer flvFile=0
#gestion des fiechiers pour queue
integer queueFile=0
integer TgpFile=0
#gestion des mp4 h264 + amr 
integer Mp4H264MP3File=0
integer H263Only=0

# =============================================================================
# Affichage
# =============================================================================

# Vert
PrintOK()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        printf "[\033[32m  OK  \033[0m]\n"
    fi
}

# Rouge
PrintFailed()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        printf "[\033[31mFAILED\033[0m]\n"
        if [ $debug -ne 0 ]
            then
            printf "=============== last log ================\n"
            tail -n 25 $LOG_FILE
            printf "=========================================\n"
        fi
    fi
}

# jaune
PrintNone()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        printf "[\033[33m NONE \033[0m]\n"
    fi
}

printLine()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        RES_COL=60
        printf "$1"
        printf "\033[%sG" $RES_COL
    fi
}

mp4_info()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        printf "========================= \033[32m File information \033[0m  =====================\n"
        cmd="${BIN_PATH}/mp4info $1 "
        $cmd
        printf "===================================================================\n"
    fi
}

start_line()
{
    if [ echo_on_stdout -eq 1 ] 
        then 
        printf "Convert $inFile to $outFile\n"
    fi
}

usage()
{
    printf "\033[1mUsage \033[0m\n"
    printf "\033[1mNAME\033[0m\n"
    printf "\t $0 convert media file to mp4 multitrack ,queue and playback formats for asterisk\n"
    printf "\033[1mSYNOPSIS\033[0m\n"
    printf "\t $0 -i infilename <-o outfilename> <-b background> <-v> <-d> <-s> <-f> <-t time> <-F> <-M> <-g> <-w> \n"
    printf "\t $0 COMMANDS\n"
    printf "\033[1mDESCRIPTION\033[0m\n"
    printf "\t\033[1m -i infilename \033[0m  Name of input file\n"
    printf "\t\033[1m -o outfilename \033[0m Optional, name of output file\n"
    printf "\t\033[1m -b background \033[0m  Optional, name of image  \n"
    printf "\t   (for media file with no video stream ) \n"
    printf "\t\033[1m -v \033[0m create log file IVES_convert.log  \n"
    printf "\t\033[1m -d \033[0m debug mode , temporary files are not deleted  \n"
    printf "\t\033[1m -s \033[0m silence mode , no output on stdout, only error  \n"
    printf "\t\033[1m -g \033[0m H263 in not good quality (qcif)  \n"
    printf "\t\033[1m -r \033[0m frame rate for background image   \n"
    printf "\t\033[1m -f \033[0m fast mode ( ratio 1:2 )  \n"
    printf "\t\033[1m -w \033[0m file informations   \n"
    printf "\t\033[1m -F \033[0m Out file are flv \n"
    printf "\t\033[1m -3 \033[0m Out file are 3gp ( h264/AMR ) \n"
    printf "\t\033[1m -M \033[0m MP4 file light in good quality H264/amr only   \n"
    printf "\t\033[1m -q \033[0m Convert input file for asterisk queue and playback formats \n"
    printf "\t\033[1m -t \033[0m time of background duration   \n"
    printf "\t\033[1m -T textfile \033[0m extract text on mp4    \n"
    printf "\t   if you dont use this , duration of background are\n"
    printf "\t   build with audio track duration \n"
    printf "\t\033[1m COMMANDS \033[0m \n"
    printf "\t\033[1m   -c | clean \033[0m remove temporary files \n"
    printf "\t   (temporary convertions , logs , and output file) \n"
    printf "\t\033[1m   -h | help \033[0m this usage \n"
    printf "\t\033[1m Version :  $Revision$  \033[0m \n"
}

# =============================================================================
# Gestion des nom de fichiers
# =============================================================================
MakeOutFilename()
{
    if [ "$outFile" == "" ]
        then 
        suffixe $inFile ;
        if [ $Mp4H264MP3File -eq 1  ]
            then outFile=`basename $inFile $mimetype`.3gp
            else if [ $flvFile -eq 1  ]
                 then outFile=`basename $inFile $mimetype`.flv
                 else if [ $TgpFile -eq 1 ]
                      then outFile=`basename $inFile $mimetype`.3gp
                      else outFile=`basename $inFile $mimetype`.mp4
                 fi
            fi
        fi
    fi
    if [ "$inFile" ==  "$outFile" ]
        then        
        printf "\033[31m Input file == Output file \033[0m\n"
        exit $EXIT_ERROR
    fi
}

MakeTempFilename()
{
    AlawFile=$base".wav"
    tmpUlawFile=$base".tmpUlaw.mulaw"
    tmpAlawFile=$base".tmpAlaw.alaw"
    tmpPcmFile=$base".pcm.wav"
    tmpMp4File=$base".tmpMp4.mp4"
    tmpMp4File_t1=$tmpMp4File".t1"    
    tmpH264File=$base".tmpH264.h264"
    tmpVideoFile=$base".tmpVideo.3gp"
    tmpStats2pnoip=$base".tmpStats2pnoip"
    tmpWorkInFile=$base".workIn.mp4"
    tmpWorkOutFile=$base".workOut.mp4"
}

# =============================================================================
# suppression des fichiers temporaires et traces
# =============================================================================

purge_log()
{
    if [ "$LOG_FILE" != "/dev/null" ]
        then
        printLine "Purge log file."
        rm -f $LOG_FILE  >> $LOG_FILE 2>&1
        ret=$?
        if [ $ret -ne 0 ] 
            then 
            PrintFailed
        else 
            PrintOK
        fi
    fi
}

clean_ctx()
{
    printLine "Clean Ctx ."
    remove_file /tmp/x264_2pass.log
    remove_file .avi.mp4 
    remove_file /tmp/ffmpeg2pass-0.log
    remove_file $tmpUlawFile
    remove_file $tmpAlawFile
    remove_file $tmpPcmFile
    remove_file $tmpMp4File
    remove_file $tmpMp4File_t1
    remove_file $tmpH264File
    remove_file $tmpStats2pnoip
    remove_file $tmpVideoFile
    remove_file $tmpWorkInFile
    remove_file $tmpWorkOrgFile
    remove_file $tmpWorkOutFile
    remove_file $INFO_FILE
    PrintOK
}

clean_old_file()
{
    printLine "Remove old file ."
    remove_file $outFile
    PrintOK
}

remove_file()
{
   file=$1 
   if [ -f $file ] 
       then
       rm -f $file >> $LOG_FILE 2>&1
       ret=$?
       if [ $ret -ne 0 ] 
           then 
           PrintFailed
           exit $EXIT_ERROR
       fi
   fi
}

# =============================================================================
# Extraction informations
# =============================================================================
suffixe() 
{
    printLine "Suffixe : "
    nom=`basename "$1"` &&
    nom=`expr match "$nom" ".*\(\..*\)$"` &&
    echo $nom
    mimetype=$nom
}

whatFile()
{
    cmd="${BIN_PATH}/ffmpeg -i  $1 "
    $cmd > $INFO_FILE 2>&1

    # mp4 ?
    grep "Input #0" $INFO_FILE | grep "mov,mp4,m4a,3gp" >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then mp4_info $1
        else 
        if [ echo_on_stdout -eq 1 ] 
            then 
            printf "========================= \033[32m File information \033[0m  =====================\n"
            $cmd
            printf "===================================================================\n"
        fi    
    fi
}

test_input_file()
{
   file=$1
   if [ "$file" == "" ]
       then 
       printf "\033[31m No input file  \033[0m\n"
       usage
       exit $EXIT_ERROR
   fi

   if [ -f $file ] 
     then echo Ok >/dev/null 
   else
     printf "\033[31m Error file  $1 not found \033[0m\n"
     usage
     exit $EXIT_ERROR
   fi
}

WhatThisFile()
{
    INFO_FILE="/tmp/.inf."`basename $inFile `
    cmd="${BIN_PATH}/ffmpeg -i  $1 "
    $cmd > $INFO_FILE 2>&1

    # mp4 ?
    grep "Input #0" $INFO_FILE | grep "mov,mp4,m4a,3gp" >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then CheckMP4File $1 std_out_ok
        else CheckFfmpegFile $1
    fi
    # Build base name 
    base="/tmp/"`basename $inFile .$mimeType`
    tmpWorkOrgFile=$base".workOrg."$mimeType

}

CheckFfmpegFile()
{
    # Extract info 
    std_out=$2
    cmd="${BIN_PATH}/ffmpeg -i $1 "
    $cmd > $INFO_FILE 2>&1
    cat  $INFO_FILE >>  $LOG_FILE
    # and check
    # Video ?
    if [ "$std_out" != "" ] ; then printLine "Video in track " ; fi 
    cmd="grep  Video: $INFO_FILE"
    $cmd  >>  $LOG_FILE ; 
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveVideo=1
          if [ "$std_out" != "" ] ; then PrintOK ; fi
          if [ "$firstCheck" -eq "0" ] ; then orgHaveVideo=1 ; fi
        else 
          haveVideo=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi
    # Audio ?
    if [ "$std_out" != "" ] ; then printLine "Audio in track " ; fi 
    cmd="grep Audio $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveAudio=1
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveAudio=0
          if [ $haveVideo -eq 0 ] 
              then PrintFailed
              exit $EXIT_ERROR
          else
              if [ "$std_out" != "" ] ; then PrintNone ; fi
          fi
    fi
    firstCheck=1 
}


CheckMP4File()
{
    # Extract info 
    std_out=$2

    cmd="${BIN_PATH}/mp4info  $1 "
    $cmd > $INFO_FILE 2>&1
    cat  $INFO_FILE >>  $LOG_FILE

    # and check
    # Video ?
    if [ "$std_out" != "" ] ; then printLine "Video in track " ; fi 
    cmd="grep video $INFO_FILE"
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveVideo=1
          if [ "$std_out" != "" ] ; then PrintOK ; fi
          if [ "$firstCheck" -eq "0" ] ; then orgHaveVideo=1 ; fi
        else 
          haveVideo=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # H263 ? 
    if [ "$std_out" != "" ] ; then printLine "Video track H263 " ; fi 
    grep video $INFO_FILE | grep H.263 >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveH263=1
          idxH263Track=`grep video $INFO_FILE | grep H.263 | awk '{print $1}'`
          idxHintH263Track=`grep H263 $INFO_FILE | grep hint | awk '{print $1}'`
          echo "idxH263Track[$idxH263Track] idxHintH263Track[$idxHintH263Track]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveH263=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # H264 ? 
    if [ "$std_out" != "" ] ; then printLine "Video track H264 " ; fi 
    grep video $INFO_FILE | grep H264 >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveH264=1
          idxH264Track=`grep video $INFO_FILE | grep H264 | awk '{print $1}'`
          idxHintH264Track=`grep H264 $INFO_FILE | grep hint | awk '{print $1}'`
          echo "idxH264Track[$idxH264Track] idxHintH264Track[$idxHintH264Track]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveH264=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # Audio ?
    if [ "$std_out" != "" ] ; then printLine "Audio in track " ; fi 
    cmd="grep audio $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveAudio=1
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveAudio=0
          if [ $haveVideo -eq 0 ] 
              then PrintFailed
              exit $EXIT_ERROR
          else
              if [ "$std_out" != "" ] ; then PrintNone ; fi
          fi
    fi

    # Ulaw ? 
    if [ "$std_out" != "" ] ; then printLine "Audio track Ulaw " ; fi 
    cmd="grep uLaw $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveUlaw=1
          idxUlawTrack=`grep uLaw $INFO_FILE | grep audio | awk '{print $1}'`
          idxHintUlawTrack=`grep PCMU $INFO_FILE | grep hint | awk '{print $1}'`
          echo "idxUlawTrack[$idxUlawTrack] idxHintUlawTrack[$idxHintUlawTrack]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveUlaw=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # Alaw ? 
    if [ "$std_out" != "" ] ; then printLine "Audio track Alaw " ; fi 
    cmd="grep aLaw $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveAlaw=1
          idxAlawTrack=`grep aLaw $INFO_FILE | grep audio | awk '{print $1}'`
          idxHintAlawTrack=`grep PCMA $INFO_FILE | grep hint | awk '{print $1}'`
          echo "idxAlawTrack[$idxAlawTrack] idxHintAlawTrack[$idxHintAlawTrack]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveAlaw=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # Amr ? 
    if [ "$std_out" != "" ] ; then printLine "Audio track Amr " ; fi 
    grep AMR $INFO_FILE | grep audio  >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveAmr=1
          idxAmrTrack=`grep AMR $INFO_FILE | grep audio  | awk '{print $1}'`
          idxHintAmrTrack=`grep AMR $INFO_FILE | grep hint | awk '{print $1}'`
          echo "idxAmrTrack[$idxAmrTrack] idxHintAmrTrack[$idxHintAmrTrack]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveAmr=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi
    firstCheck=1 
}

# =============================================================================
# Gestion audio 
# =============================================================================

create_pcm_track()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $inFile -acodec pcm_s16le -ar 8000 -ac 1 $tmpPcmFile"
    printLine "create track pcm : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi
    build_duration
}

build_duration()
{
    if [ duration -eq 0 ] 
        then 
        duration=`${BIN_PATH}/ffmpeg -i $tmpPcmFile 2>&1 | egrep Duration | awk '{print $2}' | awk -F: '{print $3}' | awk -F. '{print $1}' `
    fi
    nb_frame=`echo "($duration+1)*$frame_rate" | bc`
}



create_mulaw_track()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpPcmFile  -acodec pcm_mulaw -ar 8000 -ac 1 -f mulaw  $tmpUlawFile"
    printLine "create track mulaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi
# pour didier il faut virer ce bout de code aprés ;-)
 
#   cmd="${BIN_PATH}/ffmpeg -y -i $tmpPcmFile  -acodec pcm_s16le -ar 8000 -ac 1  $AlawFile"
#    printLine "create alaw file : "
#    echo $cmd >> $LOG_FILE
#    $cmd >> $LOG_FILE 2>&1
#    ret=$?
#    if [ $ret -ne 0 ]
#    then
#        PrintFailed
#        exit $EXIT_ERROR
#    else
#        PrintOK
#    fi
 
}

add_mulaw_track()
{
    #cmd="${BIN_PATH}/ffmpeg -i $tmpWorkInFile -i $tmpUlawFile $tmpWorkOutFile"
    cmd="${BIN_PATH}/pcm2mp4 $tmpUlawFile  $tmpWorkInFile"
    printLine "Add track mulaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        # rm -f $tmpWorkInFile
        # mv $tmpWorkOutFile $tmpWorkInFile
        CheckMP4File $tmpWorkInFile
    fi 
}

hint_mulaw_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxUlawTrack $tmpWorkInFile "
    printLine "Hint track mulaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

create_alaw_track()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpPcmFile  -acodec pcm_alaw -ar 8000 -ac 1 -f mulaw $tmpAlawFile"
    printLine "create track alaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi 
}

add_alaw_track()
{
    cmd="${BIN_PATH}/pcm2mp4 $tmpAlawFile  $tmpWorkInFile"
    printLine "Add track alaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

hint_alaw_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxAlawTrack $tmpWorkInFile "
    printLine "Hint track mulaw : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

hint_amr_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxAmrTrack $tmpWorkInFile "
    printLine "Hint track amr : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

AddAudioTracks()
{
    if [ haveUlaw -eq 0 ] 
        then 
        create_mulaw_track 
        add_mulaw_track
        #hint_mulaw_track 
    fi 
    if [ haveAlaw -eq 0 ] 
        then 
        create_alaw_track 
        add_alaw_track
        #hint_alaw_track 
    fi         
    if [ idxHintUlawTrack -eq 0 ] 
        then hint_ulaw_track
    fi
    if [ idxHintAlawTrack -eq 0 ] 
        then hint_alaw_track
    fi
    if [ idxHintAmrTrack -eq 0 ] 
        then hint_amr_track
    fi
}

# =============================================================================
# Gestion video flv
# =============================================================================
create_flv_file()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $inFile -s 320x240 -ar 22050 -r 25 -f flv  $outFile"
    printLine "Create flv file : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}

create_flv_file_from_org()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile -s 320x240 -ar 22050 -r 25 -f flv  $outFile"
    printLine "Create flv file (from org) : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}

# =============================================================================
# Gestion video mp4 good quality h264 and audio amr only 
# =============================================================================
create_Mp4H264MP3_file()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $inFile -vcodec libx264  -acodec libamr_nb -ac 1 -ab 12200 -ar 8000   $outFile"
    printLine "Create mp4 light ( h264/amr ) file : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}

create_Mp4H264MP3_file_from_org()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile -vcodec libx264 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000  $outFile"
    printLine "Create mp4 light ( h264/amr ) file (from org) : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}

# =============================================================================
# Gestion 3gp 
# =============================================================================
create_3gp_file()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $inFile  $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -ar 22050 -r 25 -ar 8000 -ab 12.2k -ac 1 $outFile"
    printLine "Create 3gp file : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}

create_3gp_file_from_org()
{
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile  $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -ar 22050 -r 25 -ar 8000 -ab 12.2k -ac 1 $outFile "
    printLine "Create 3gp file (from org) : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
}
# =============================================================================
# Gestion video 
# =============================================================================

AddVideoBackground()
{
    if [ "$backgroundFile" != "" ]
        then
        test_input_file $backgroundFile
        create_mulaw_track
        cmd="${BIN_PATH}/ffmpeg  -loop_input -vframes $nb_frame -i $backgroundFile -i $tmpPcmFile -r $frame_rate -vb 40k -ab 10.2k -s qcif -i_qfactor 0.5   -acodec libamr_nb -ac 1 -ab 12200  $tmpVideoFile"
        printLine "Add video  : "
        echo $cmd >> $LOG_FILE
        $cmd >> $LOG_FILE 2>&1
        ret=$?
        if [ $ret -ne 0 ] 
            then 
            PrintFailed
            exit $EXIT_ERROR
        else 
            PrintOK
            rm -f $tmpWorkInFile  >> $LOG_FILE 2>&1
            mv $tmpVideoFile $tmpWorkInFile
            inFile=$tmpWorkInFile
            CheckMP4File $tmpWorkInFile 
        fi
    fi
}
############################## H263 Bad Quality 3G  #####################################################
create_H263_track()
{
    cd /tmp
    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 -r $V_FPS_H263 \
         -vcodec h263 -b $V_BITRATE_H263 -bt $V_BR_TOLERANCE_H263 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
        else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE -r 7 -vcodec h263 -b $V_BITRATE \
         -bt 10000 -vstats -ar 8000 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 -vstats_file \
         $tmpStats2pnoip -pass 1  $tmpVideoFile "
    fi
    printLine "Create track H263 pass 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 -r $V_FPS_H263 \
         -vcodec h263 -b  $V_BITRATE_H263 -bt $V_BR_TOLERANCE_H263 -vstats -vstats_file  \
         $tmpStats2pnoip -pass 2 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE -r 7 -vcodec h263 -b $V_BITRATE \
         -bt 10000 -vstats -ar 8000 -acodec libamr_nb -ac 1 -ab 12200 -vstats_file  \
         $tmpStats2pnoip -pass 2 -ar 8000 $tmpVideoFile "
    fi
    printLine "Create track H263 pass 2 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintNone
        create_H263_on_pass_track
    else 
        PrintOK
    fi

    rm -f $tmpStats2pnoip
    rm -f $tmpWorkInFile  >> $LOG_FILE 2>&1
    mv $tmpVideoFile $tmpWorkInFile
    inFile=$tmpWorkInFile
    CheckMP4File $tmpWorkInFile 

    cd - >/dev/null 2>&1
}

create_H263_on_pass_track()
{
    cd /tmp
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 \
         -r 15 -vcodec h263 -b 66000 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    printLine "Create track H263 with single pass : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
    cd - >/dev/null 2>&1
}

hint_H263_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxH263Track $tmpWorkInFile "
    printLine "Hint track H263 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}
############################## H263 GOOD QUALITY  #####################################################

create_H263_good_track()
{
    cd /tmp
    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD -r $V_FPS_H263_GOOD \
         -vcodec h263 -b $V_BITRATE_H263_GOOD -bt $V_BR_TOLERANCE_H263_GOOD -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
        else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE_CIF -r 7 -vcodec h263 -b $V_BITRATE_H263_GOOD \
         -bt 10000 -vstats -ar 8000 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 -vstats_file \
         $tmpStats2pnoip -pass 1  $tmpVideoFile "
    fi
    printLine "Create track H263 good pass 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD -r $V_FPS_H263_GOOD \
         -vcodec h263 -b  $V_BITRATE_H263_GOOD -bt $V_BR_TOLERANCE_H263_GOOD -vstats -vstats_file  \
         $tmpStats2pnoip -pass 2 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE_CIF -r $V_FPS_H263_GOOD  -vcodec h263 -b $V_BITRATE_GOOD \
         -bt 10000 -vstats -ar 8000 -acodec libamr_nb -ac 1 -ab 12200 -vstats_file  \
         $tmpStats2pnoip -pass 2 -ar 8000 $tmpVideoFile "
    fi
    printLine "Create track H263 good  pass 2 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintNone
        create_H263_good_on_pass_track
    else 
        PrintOK
    fi

    rm -f $tmpStats2pnoip
    rm -f $tmpWorkInFile  >> $LOG_FILE 2>&1
    mv $tmpVideoFile $tmpWorkInFile
    inFile=$tmpWorkInFile
    CheckMP4File $tmpWorkInFile 

    cd - >/dev/null 2>&1
}

create_H263_good_on_pass_track()
{
    cd /tmp
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD \
         -r 15 -vcodec h263 -b 66000 -acodec libamr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    printLine "Create track H263 good with single pass : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi
    cd - >/dev/null 2>&1
}

hint_H263_good_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxH263Track $tmpWorkInFile "
    printLine "Hint track H263 good: "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

############################## H264 #####################################################

create_H264_track()
{
    cd /tmp

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file \
         $tmpStats2pnoip  -pass 1 $tmpMp4File"
    else
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE -r V_FPS_H264 -vcodec libx264 -b $V_BITRATE \
         -bt 10000 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec libamr_nb -ac 1 -ab 12200 $tmpVideoFile"
    fi
    printLine "Create track H264 pass 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file  \
          $tmpStats2pnoip -pass 2 $tmpMp4File"
    else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkInFile -s $V_SIZE -r V_FPS_H264 -vcodec libx264 -b $V_BITRATE \
         -bt 10000 -vstats -vstats_file  \
         $tmpStats2pnoip  -pass 2 -acodec libamr_nb -ac 1 -ab 12200  $tmpVideoFile"
    fi
    printLine "Create track H264 pass 2 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    if [ mode_fast -ne 0 ]
        then
        rm -f $tmpMp4File
        cp $tmpVideoFile $tmpMp4File
    fi

    rm -f $tmpStats2pnoip
    cmd="${BIN_PATH}/mp4creator -extract=1 $tmpMp4File"
    printLine "H264 extract track 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    rm -f $tmpMp4File
    mv $tmpMp4File_t1 $tmpH264File

    cmd="${BIN_PATH}/mp4creator -create $tmpH264File -rate $V_FPS_H264 $tmpWorkInFile"
    printLine "create H264  : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi
    rm -f $tmpH264File
    cd - >/dev/null 2>&1
}

create_H264_track_from_org()
{
    cd /tmp

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file \
         $tmpStats2pnoip  -pass 1 $tmpMp4File"
    else
    cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile -s $V_SIZE -r V_FPS_H264 -vcodec libx264 -b $V_BITRATE \
         -bt 10000 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec libamr_nb -ac 1 -ab 12200 $tmpVideoFile"
    fi
    printLine "Create track H264 (from org) pass 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    if [ mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file  \
          $tmpStats2pnoip -pass 2 $tmpMp4File"
    else
        cmd="${BIN_PATH}/ffmpeg -y -i $tmpWorkOrgFile -s $V_SIZE -r V_FPS_H264 -vcodec libx264 -b $V_BITRATE \
         -bt 10000 -vstats -vstats_file  \
         $tmpStats2pnoip  -pass 2 -acodec libamr_nb -ac 1 -ab 12200  $tmpVideoFile"
    fi
    printLine "Create track H264 (from org) pass 2 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    if [ mode_fast -ne 0 ]
        then
        rm -f $tmpMp4File
        cp $tmpVideoFile $tmpMp4File
    fi

    rm -f $tmpStats2pnoip
    cmd="${BIN_PATH}/mp4creator -extract=1 $tmpMp4File"
    printLine "H264 (from org) extract track 1 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?

    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
    fi

    rm -f $tmpMp4File
    mv $tmpMp4File_t1 $tmpH264File

    cmd="${BIN_PATH}/mp4creator -create $tmpH264File -rate $V_FPS_H264 $tmpWorkInFile"
    printLine "create (from org) H264  : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        PrintFailed
        exit
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi
    rm -f $tmpH264File
    cd - >/dev/null 2>&1
}

hint_H264_track()
{
    cmd="${BIN_PATH}/mp4creator -hint=$idxH264Track $tmpWorkInFile "
    printLine "Hint track H264 : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
        CheckMP4File $tmpWorkInFile 
    fi 
}

AddVideoTracks()
{
   if  [ $haveVideo -eq 0 ]
        then AddVideoBackground
   fi

   if  [ $haveVideo -ne 0 ]
       then 
       if [ haveH263 -eq 0 ] 
           then 
           if [ h263_good -eq 0 ]
               then create_H263_track 
               else create_H263_good_track
           fi  
       fi
       # for amr ..... 
       if [ haveAmr -eq 0 ]
           then 
           if [ $haveAudio -ne 0 ] 
               then 
               if [ h263_good -eq 0 ]
                   then create_H263_track 
                   else create_H263_good_track
               fi
           fi
       fi 
       if [ haveH264 -eq 0 ] 
           then 
           if [ orgHaveVideo -eq 1 ]
               then create_H264_track_from_org
               else create_H264_track 
           fi
       fi 
       if [ idxHintH264Track -eq 0 ] 
           then hint_H264_track
       fi
       if [ idxHintH263Track -eq 0 ] 
           then hint_H263_track
       fi
   fi
}
# =============================================================================
# gestion du texte
# =============================================================================
ExtractTextFromMp4()
{
    if [ "$outTxtName" != "" ]
        then
        cmd="${BIN_PATH}/mp4info $orgFile "
        printLine "Extract text informations "
        $cmd > $INFO_FILE 2>&1
        integer begin=`grep -n Comment $INFO_FILE | awk -F: '{print $1}'`      
        if [ $begin -ne 0 ]
            then 
            integer end=`wc $INFO_FILE|awk '{print $1}'`
            integer nb=$end-$begin
            tail -n $nb   $INFO_FILE >  $outTxtName 
            ret=$?
            if [ $ret -ne 0 ] 
                then 
                PrintFailed
                exit $EXIT_ERROR
            else 
                PrintOK
            fi
        fi        
    fi
}

HaveAllTrack()
{
    if [ haveUlaw -eq 1 ] ; then 
        if [ haveAlaw -eq 1 ] ; then 
            if [ haveAmr -eq 1 ] ; then 
                if [ haveH263 -eq 1 ] ; then 
                    if [ haveH264 -eq 1 ] ; then 
                        # rien a faire 
                        cp $inFile $outFile
                        mp4_info $outFile
                        exit $EXIT_SUCCESS
                    fi
                fi
            fi
        fi
    fi
}

CopyTmp2out()
{
    cd $ORG_REP
    mv $tmpWorkInFile $outFile
    ret=$?
    if [ $ret -ne 0 ] 
        then 
        exit $EXIT_ERROR
    fi
}

CopyIn2tmp()
{
    ORG_REP=$PWD
    cp $inFile $tmpWorkInFile
    cp $inFile $tmpWorkOrgFile
}

MakeMp4()
{
    HaveAllTrack
    ExtractTextFromMp4
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    AddVideoTracks
    if [ $haveAudio -ne 0 ] 
         then AddAudioTracks 
    fi
    CopyTmp2out
    whatFile $outFile
}

MakeFlv()
{
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    if  [ $haveVideo -eq 0 ]
        then AddVideoBackground
    fi

    if [ orgHaveVideo -eq 1 ]
        then create_flv_file_from_org
        else create_flv_file 
    fi
    whatFile $outFile    
}

MakeMp4H264MP3()
{
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    if  [ $haveVideo -eq 0 ]
        then AddVideoBackground
    fi

    if [ orgHaveVideo -eq 1 ]
        then create_Mp4H264MP3_file_from_org
        else create_Mp4H264MP3_file 
    fi
    whatFile $outFile    
}

MakeH263Only()
{
    CopyIn2tmp
    if [ h263_good -eq 0 ]
      then create_H263_track 
      else create_H263_good_track
    fi
    cp $tmpWorkInFile $outFile
    whatFile $outFile    
}

Make3gp()
{
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    if  [ $haveVideo -eq 0 ]
        then AddVideoBackground
    fi

    if [ orgHaveVideo -eq 1 ]
        then create_3gp_file_from_org
        else create_3gp_file 
    fi
    whatFile $outFile    
}

MakeQueueFile()
{
    CopyIn2tmp
    if [ $h263_good -eq 0 ]
        then IVES_convert.ksh -i $inFile -o /tmp/.queueFile.mp4
        else IVES_convert.ksh -i $inFile -o /tmp/.queueFile.mp4 -g
    fi
    mv /tmp/.queueFile.mp4 $outFile
    cmd="${BIN_PATH}/mp4asterisk $outFile "
    printLine "Creation file for asterisk Queue and Playback app : "
    echo $cmd >> $LOG_FILE
    $cmd >> $LOG_FILE 2>&1
    ret=$?
    if [ $ret -ne 0 ] 
    then 
        PrintFailed
        exit $EXIT_ERROR
    else 
        PrintOK
    fi     
    if [ $haveAudio -ne 0 ] 
        then 
        create_pcm_track 
        wavname=`basename $inFile $mimetype`.wav
        cp $tmpPcmFile $wavname
    fi 
}



Execut()
{
    test_input_file $inFile
    MakeOutFilename
    start_line
    WhatThisFile $inFile std_out_ok
    MakeTempFilename
    clean_ctx
    clean_old_file
    purge_log
    if [ $Mp4H264MP3File -eq 1  ]
        then MakeMp4H264MP3
        else if [ $flvFile -eq 1  ]
             then MakeFlv
             else if [ $queueFile -eq 1  ]
                  then MakeQueueFile
                  else if [ $TgpFile -eq 1  ]
                       then Make3gp
                       else if [ $H263Only -eq 1 ]
                            then MakeH263Only
                            else MakeMp4
                       fi
                  fi
             fi
        fi
    fi
    if [ $debug -eq 0 ] ; then clean_ctx ; fi
}



# =============================================================================
# main : parse args and exeute 
# =============================================================================

while [ "$1" ] 
  do    
  case "$1" in
      -g)
      h263_good=0
      ;;
      -h263)
      H263Only=1
      ;;
      -i)
      shift
      inFile=$1
      orgFile=$inFile
      ;;
      -o)
      shift
      outFile=$1
      ;;
      -F)
      flvFile=1
      ;;
      -M)
      Mp4H264MP3File=1
      ;;
      -r)
      shift
      frame_rate=$1 
      ;;
      -b)
      shift
      backgroundFile=$1
      ;;
      -v)
      LOG_FILE="/tmp/IVES_convert.log"
      ;;
      -d)
      debug=1
      LOG_FILE="/tmp/IVES_convert.log"
      ;;
      -s)
      echo_on_stdout=0
      ;;
      -f)
      mode_fast=1
      ;;
      -t)
      shift
      duration=$1
      ;;
      -T)
      shift 
      outTxtName=$1
      ;;
      -w)
      test_input_file  $inFile
      whatFile $inFile
      exit $EXIT_SUCCESS
      ;;
      -c|clean)
      MakeOutFilename
      MakeTempFilename
      purge_log
      clean_ctx
      exit $EXIT_SUCCESS
      ;;
      -h|help)
      usage
      exit $EXIT_SUCCESS
      ;;
      -q)
      queueFile=1
      ;;
      -3)
      TgpFile=1
      ;;
      *)
      echo "Commande inconnue: $1"
      usage
      exit $EXIT_ERROR
      ;;
  esac
  shift
done
Execut
