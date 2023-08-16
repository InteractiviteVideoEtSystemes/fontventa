#!/bin/bash
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


V_SIZE_H264="vga"
V_FPS_H264=25
V_BITRATE_H264=340000
V_BR_TOLERANCE_H264=30000
V_FFMPEG_OPTS_H264="-g 250 -slice-max-size 1300 -level 20 -qmax 38 -me_method hex "

# =============================================================================
# Constant de travail
# =============================================================================
BIN_PATH="/usr/bin"
BIN_FFMPEG="ffmpeg"
INFO_FILE="/tmp/IVES_convert.inf"
EXIT_SUCCESS=0
EXIT_ERROR=1
#gestion des logs
LOG_FILE="/dev/null"

# =============================================================================
# Constant de travail
# =============================================================================
# flag de debug
debug=0
declare -i echo_on_stdout=1
mode_fast=0
# contenue des track
mimeType="other"
rtpStat=""
haveVideo=0
haveAudio=0
orgHaveVideo=0
firstCheck=0
# information audio
haveUlaw=0
idxUlawTrack=0
idxHintUlawTrack=0
haveAlaw=0
idxAlawTrack=0
idxHintAlawTrack=0
haveAmr=0
idxAmrTrack=0
idxHintAmrTrack=0
haveH263=0
idxH263Track=0
idxHintH263Track=0
h263_good=1
haveH264=0
idxH264Track=0
idxHintH264Track=0
# calcul duree video
duration=0
nb_frame=0
frame_rate=3 
# extraction du texte 
text=0
#gestion du flv
flvFile=0
#gestion des fiechiers pour queue
queueFile=0
Html5File=0
#gestion des mp4 h264 + amr 
Mp4H264MP3File=0
H263Only=0
WebMOnly=0

# =============================================================================
# Affichage
# =============================================================================

# Vert
PrintOK()
{
    if [ $echo_on_stdout -eq 1 ] 
        then 
        printf "[\033[32m  OK  \033[0m]\n"
    fi
}

# Rouge
PrintFailed()
{
    if [ $echo_on_stdout -eq 1 ] 
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
    if [ $echo_on_stdout -eq 1 ] 
        then 
        printf "[\033[33m NONE \033[0m]\n"
    fi
}

printLine()
{
    if [ $echo_on_stdout -eq 1 ] 
        then 
        RES_COL=60
        printf "$1"
        printf "\033[%sG" $RES_COL
    fi
}

mp4_info()
{
    if [ $echo_on_stdout -eq 1 ] 
        then 
        printf "========================= \033[32m File information \033[0m  =====================\n"
        cmd="${BIN_PATH}/mp4info $1 "
        $cmd
        if [ "$rtpStat" != "" ] 
            then 
            printf "======================  RTP Stat  =================================\n"  
            cat $1.stat
        fi
        printf "===================================================================\n"
    fi
}

start_line()
{
    if [ $echo_on_stdout -eq 1 ] 
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
    printf "\t\033[1m -5 \033[0m MP4 HTML5 compliant in VGA  \n"
    printf "\t\033[1m -r \033[0m frame rate for background image   \n"
    printf "\t\033[1m -f \033[0m fast mode ( ratio 1:2 )  \n"
    printf "\t\033[1m -w \033[0m file informations   \n"
    printf "\t\033[1m -F \033[0m Out file are flv \n"
    printf "\t\033[1m -M \033[0m MP4 file light in good quality H264/amr only   \n"
    printf "\t\033[1m -q \033[0m Convert input file for asterisk queue and playback formats \n"
    printf "\t\033[1m -t \033[0m time of background duration   \n"
    printf "\t\033[1m -T textfile \033[0m extract text on mp4    \n"
    printf "\t\033[1m -webm \033[0m  only convert WebM file    \n"
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
                 else if [ $Html5File -eq 1 ]
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -i  $1 "
    $cmd > $INFO_FILE 2>&1

    # mp4 ?
    grep "Input #0" $INFO_FILE | grep "mov,mp4,m4a,3gp" >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then mp4_info $1
        else 
        if [ $echo_on_stdout -eq 1 ] 
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -i  $1 "
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -i $1 "
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
	  grep video $INFO_FILE | grep H.263 | awk '{print $1}' > $INFO_FILE.idxH263Track 2>&1
          grep H263 $INFO_FILE | grep hint | awk '{print $1}' > $INFO_FILE.idxHintH263Track 2>&1           

	  idxH263Track=`cat $INFO_FILE.idxH263Track`
          idxHintH263Track=`cat $INFO_FILE.idxHintH263Track`
	  rm -rf $INFO_FILE.idxH263Track $INFO_FILE.idxHintH263Track

          echo "idxH263Track[$idxH263Track] idxHintH263Track[$idxHintH263Track]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveH263=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # H264 ? 
    #set -x
    if [ "$std_out" != "" ] ; then printLine "Video track H264 " ; fi 
    grep video $INFO_FILE | grep H264 >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveH264=1
          grep video $INFO_FILE | grep H264 | awk '{print $1}' > $INFO_FILE.idxH264Track 2>&1
          grep H264 $INFO_FILE | grep hint | awk '{print $1}' > $INFO_FILE.idxHintH264Track 2>&1

          idxH264Track=`cat $INFO_FILE.idxH264Track` 
          idxHintH264Track=`cat $INFO_FILE.idxHintH264Track` 
          rm -f $INFO_FILE.idxH264Track $INFO_FILE.idxHintH264Track 

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
    cmd="grep -i uLaw $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveUlaw=1
	  grep ulaw $INFO_FILE | grep audio | awk '{print $1}' > $INFO_FILE.idxUlawTrack 2>&1
          grep PCMU $INFO_FILE | grep hint | awk '{print $1}' > $INFO_FILE.idxHintUlawTrack 2>&1
         
	  idxUlawTrack=`cat $INFO_FILE.idxUlawTrack`
	  idxHintUlawTrack=`cat $INFO_FILE.idxHintUlawTrack` 
	  rm -rf $INFO_FILE.idxUlawTrack $INFO_FILE.idxHintUlawTrack

          echo "idxUlawTrack[$idxUlawTrack] idxHintUlawTrack[$idxHintUlawTrack]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveUlaw=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi

    # Alaw ? 
    if [ "$std_out" != "" ] ; then printLine "Audio track Alaw " ; fi 
    cmd="grep -i aLaw $INFO_FILE" 
    $cmd >>  $LOG_FILE
    ret=$? 
    if [ "$ret" -eq "0" ]
        then 
          haveAlaw=1
          grep aLaw $INFO_FILE | grep audio | awk '{print $1}' > $INFO_FILE.idxAlawTrack 2>&1 
	  grep PCMA $INFO_FILE | grep hint | awk '{print $1}' > $INFO_FILE.idxHintAlawTrack 2>&1
   
          idxAlawTrack=`cat $INFO_FILE.idxAlawTrack`
          idxHintAlawTrack=`cat $INFO_FILE.idxHintAlawTrack`
	  rm -rf $INFO_FILE.idxAlawTrack $INFO_FILE.idxHintAlawTrack
		
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
	  grep AMR $INFO_FILE | grep audio | awk '{print $1}' > $INFO_FILE.idxAmrTrack 2>&1
          grep AMR $INFO_FILE | grep hint | awk '{print $1}' > $INFO_FILE.idxHintAmrTrack 2>&1

          idxAmrTrack=`cat $INFO_FILE.idxAmrTrack` 
	  idxHintAmrTrack=`cat $INFO_FILE.idxHintAmrTrack`
	  rm -rf $INFO_FILE.idxAmrTrack $INFO_FILE.idxHintAmrTrack

          echo "idxAmrTrack[$idxAmrTrack] idxHintAmrTrack[$idxHintAmrTrack]" >> $LOG_FILE
          if [ "$std_out" != "" ] ; then PrintOK ; fi
        else 
          haveAmr=0
          if [ "$std_out" != "" ] ; then PrintNone ; fi
    fi
    firstCheck=1 
}

ExtractRtpStatOnMp4()
{

    cmd="${BIN_PATH}/mp4info  $orgFile "
    $cmd > $INFO_FILE 2>&1
    cat  $INFO_FILE >>  $LOG_FILE
    printLine "Extract rtp stat on file"
    grep -n Album $INFO_FILE | awk -F":" '{print $3}' > $INFO_FILE.rtpStat 2>&1
    ret=$?
    rtpStat=`cat $INFO_FILE.rtpStat`
    rm -rf $INFO_FILE.rtpStat 
    if [ $ret -ne 0 ] 
        then 
        PrintNone
    else
        PrintOK
    fi
}

# =============================================================================
# Gestion audio 
# =============================================================================

create_pcm_track()
{
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $inFile -acodec pcm_s16le -ar 8000 -ac 1 $tmpPcmFile"
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
    if [ $duration -eq 0 ] 
        then 
	${BIN_PATH}/${BIN_FFMPEG} -i $tmpPcmFile 2>&1 | egrep Duration | awk '{print $2}' | awk -F: '{print $3}' | awk -F. '{print $1}' > $INFO_FILE.duration 2>&1
	duration=`cat $INFO_FILE.duration`
	rm -f $INFO_FILE.duration
    fi
    	echo "($duration+1)*$frame_rate" | bc > $INFO_FILE.nb_frame 2>&1   
	nb_frame=`cat $INFO_FILE.nb_frame`
 	rm -f $INFO_FILE.nb_frame
}



create_mulaw_track()
{
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpPcmFile  -acodec pcm_mulaw -ar 8000 -ac 1 -f mulaw  $tmpUlawFile"
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
 
#   cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpPcmFile  -acodec pcm_s16le -ar 8000 -ac 1  $AlawFile"
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
    #cmd="${BIN_PATH}/${BIN_FFMPEG} -i $tmpWorkInFile -i $tmpUlawFile $tmpWorkOutFile"
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpPcmFile  -acodec pcm_alaw -ar 8000 -ac 1 -f mulaw $tmpAlawFile"
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
    if [ $haveUlaw -eq 0 ] 
        then 
        create_mulaw_track 
        add_mulaw_track
        #hint_mulaw_track 
    fi 
    if [ $haveAlaw -eq 0 ] 
        then 
        create_alaw_track 
        add_alaw_track
        #hint_alaw_track 
    fi         
    if [ $idxHintUlawTrack -eq 0 ] 
        then hint_ulaw_track
    fi
    if [ $idxHintAlawTrack -eq 0 ] 
        then hint_alaw_track
    fi
    if [ "$idxHintAmrTrack" == "" ] 
        then hint_amr_track
    fi
}

# =============================================================================
# Gestion video flv
# =============================================================================
create_flv_file()
{
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $inFile -s 320x240 -ar 22050 -r 25 -f flv  $outFile"
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile -s 320x240 -ar 22050 -r 25 -f flv  $outFile"
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $inFile -vcodec libx264  -acodec amr_nb -ac 1 -ab 12200 -ar 8000   $outFile"
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile -vcodec libx264 -acodec amr_nb -ac 1 -ab 12200 -ar 8000  $outFile"
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
# Gestion HTML5 
# =============================================================================
create_html5_file()
{
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $inFile  $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -acodec aac -ac 1 -ar 32000 $outFile"
    printLine "Create MP4/HTML file : "
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

create_html5_file_from_org()
{
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile  $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -vcodec copy -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -acodec aac -ar 32000 -ac 1 $outFile "
    printLine "Create HTML5/MP4 file (from org) : "
    echo $cmd >> $LOG_FILE
    $cmd #>> $LOG_FILE 2>&1
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
        #cmd="${BIN_PATH}/${BIN_FFMPEG}  -loop 1 -i $backgroundFile -i $tmpPcmFile -pix_fmt yuv420p -r $frame_rate -vcodec libx264 -ac 1 -force_key_frames \"expr:gte(t,n_forced)\" -vframes 20 $tmpVideoFile"
        cmd="${BIN_PATH}/${BIN_FFMPEG}  -loop 1 -i $backgroundFile -i $tmpPcmFile -pix_fmt yuv420p -r $frame_rate -vcodec libx264 -ac 1 -vframes 10 $tmpVideoFile"
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
    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 -r $V_FPS_H263 \
         -vcodec h263 -b:v $V_BITRATE_H263 -bt $V_BR_TOLERANCE_H263 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
        else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE -r 7 -vcodec h263 -b:v $V_BITRATE \
         -bt 10000 -vstats -ar 8000 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 -vstats_file \
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 -r $V_FPS_H263 \
         -vcodec h263 -b:v  $V_BITRATE_H263 -bt $V_BR_TOLERANCE_H263 -vstats -vstats_file  \
         $tmpStats2pnoip -pass 2 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE -r 7 -vcodec h263 -b:v $V_BITRATE \
         -bt 10000 -vstats -ar 8000 -acodec amr_nb -ac 1 -ab 12200 -vstats_file  \
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263 -s $V_SIZE_H263 \
         -r 15 -vcodec h263 -b:v 66000 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
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
    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD -r $V_FPS_H263_GOOD \
         -vcodec h263 -b:v $V_BITRATE_H263_GOOD -bt $V_BR_TOLERANCE_H263_GOOD -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
        else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE_CIF -r 7 -vcodec h263 -b:v $V_BITRATE_H263_GOOD \
         -bt 10000 -vstats -ar 8000 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 -vstats_file \
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD -r $V_FPS_H263_GOOD \
         -vcodec h263 -b:v  $V_BITRATE_H263_GOOD -bt $V_BR_TOLERANCE_H263_GOOD -vstats -vstats_file  \
         $tmpStats2pnoip -pass 2 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
    else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE_CIF -r $V_FPS_H263_GOOD  -vcodec h263 -b:v $V_BITRATE_GOOD \
         -bt 10000 -vstats -ar 8000 -acodec amr_nb -ac 1 -ab 12200 -vstats_file  \
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
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H263_GOOD -s $V_SIZE_H263_GOOD \
         -r 15 -vcodec h263 -b:v 66000 -acodec amr_nb -ac 1 -ab 12200 -ar 8000 $tmpVideoFile "
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file \
         $tmpStats2pnoip  -pass 1 $tmpMp4File"
    else
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE \
         -bt 10000 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec amr_nb -ac 1 -ab 12200 $tmpVideoFile"
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file  \
          $tmpStats2pnoip -pass 2 $tmpMp4File"
    else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile -s $V_SIZE -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE \
         -bt 10000 -vstats -vstats_file  \
         $tmpStats2pnoip  -pass 2 -acodec amr_nb -ac 1 -ab 12200  $tmpVideoFile"
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

    if [ $mode_fast -ne 0 ]
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file \
         $tmpStats2pnoip  -pass 1 $tmpMp4File"
    else
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile -s $V_SIZE -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE \
         -bt 10000 -vstats -vstats_file \
         $tmpStats2pnoip -pass 1 -acodec amr_nb -ac 1 -ab 12200 $tmpVideoFile"
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

    if [ $mode_fast -eq 0 ]
        then 
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -an -vstats -vstats_file  \
          $tmpStats2pnoip -pass 2 $tmpMp4File"
    else
        cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkOrgFile -s $V_SIZE -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE \
         -bt 10000 -vstats -vstats_file  \
         $tmpStats2pnoip  -pass 2 -acodec amr_nb -ac 1 -ab 12200  $tmpVideoFile"
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

    if [ $mode_fast -ne 0 ]
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
       # if [ $haveH263 -eq 0 ] 
       # then 
       #    create_H263_good_track
       # fi
       # for amr ..... 
       #if [ $haveAmr -eq 0 ]
       #    then 
           #if [ $haveAudio -ne 0 ] 
           #then 
           #    create_H263_good_track
           #fi
       #fi 
       if [ $haveH264 -eq 0 ] 
           then 
           if [ $orgHaveVideo -eq 1 ]
               then create_H264_track_from_org
               else create_H264_track 
           fi
       fi 
       if [ "$idxHintH264Track" == "" ] 
           then hint_H264_track
       fi
       #if [ "$idxHintH263Track" == "" ] 
       #    then hint_H263_track
       #fi
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
        $cmd | awk 'BEGIN  { marq = 0; }
{
   if ( $0 ~ / Comments:/)
   {
                marq = 1;
   }
   else
   {
                if (marq > 0) print $0;
   }
}' > $outTxtName

        ret=$?
        if [ $ret -ne 0 ] 
                then 
                PrintFailed
                exit $EXIT_ERROR
        else 
		cat $outTxtName
                PrintOK
        fi        
    fi
}

HaveAllTrack()
{
    if [ $haveUlaw -eq 1 ] ; then 
        if [ $haveAlaw -eq 1 ] ; then 
            if [ $haveAmr -eq 1 ] ; then 
                if [ $haveH263 -eq 1 ] ; then 
                    if [ $haveH264 -eq 1 ] ; then 
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
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    AddVideoTracks
    if [ $haveAudio -ne 0 ] 
         then AddAudioTracks 
    fi

    ExtractRtpStatOnMp4  
    if [ "$rtpStat" != "" ] 
        then echo $rtpStat > $outFile.stat 
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

MakeWebMOnly()
{
    CopyIn2tmp
    cmd="${BIN_PATH}/${BIN_FFMPEG} -y -i $tmpWorkInFile  $V_FFMPEG_OPTS_H264 -s $V_SIZE_H264 -r $V_FPS_H264 -vcodec libx264 -b:v $V_BITRATE_H264 \
         -bt $V_BR_TOLERANCE_H264 -acodec aac -ac 1 -ar 32000 -strict -2 $tmpVideoFile"
    $cmd > $INFO_FILE 2>&1
    cp $tmpWorkInFile $outFile
    whatFile $outFile    
}

MakeHtml5()
{
    CopyIn2tmp
    if [ $haveAudio -ne 0 ] 
         then create_pcm_track 
    fi
    if  [ $haveVideo -eq 0 ]
        then AddVideoBackground
    fi

    if [ $orgHaveVideo -eq 1 ]
        then create_html5_file_from_org
        else create_html5_file 
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



Execute()
{
    test_input_file $inFile
    MakeOutFilename
    start_line
    ExtractTextFromMp4
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
                  else if [ $Html5File -eq 1  ]
                       then MakeHtml5
                       else if [ $WebMOnly -eq 1 ]
                            then MakeWebMOnly
                            else if [ $H263Only -eq 1 ]
                                    then MakeH263Only
                                    else MakeMp4
                            fi
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
      -webm)
      WebMOnly=1
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
      -5)
      Html5File=1
      ;;
      *)
      echo "Commande inconnue: $1"
      usage
      exit $EXIT_ERROR
      ;;
  esac
  shift
done
Execute
