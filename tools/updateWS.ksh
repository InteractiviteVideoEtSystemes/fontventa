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
# brief    Convert media file for Hawk Waitscreen
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
# Constant de travail
# =============================================================================
BIN_PATH="/usr/bin"
EXIT_SUCCESS=0
EXIT_ERROR=1
echo_on_stdout=1

# =============================================================================
# Constant de travail
# =============================================================================
MediaRep=/data/asterisk/prod/sounds/
MediaName=waitscreen.mp4
SaveMediaName=waitscreen.sav.mp4
TmpMediaName=waitscreen.tmp.mp4

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

usage()
{
    printf "\033[1mUsage \033[0m\n"
    printf "\033[1mNAME\033[0m\n"
    printf "\t $0  Convert media file for Hawk wait screen\n"
    printf "\033[1mSYNOPSIS\033[0m\n"
    printf "\t $0 -i infilename <-h> \n"
    printf "\033[1mDESCRIPTION\033[0m\n"
    printf "\t\033[1m -i infilename \033[0m  Name of input file\n"
    printf "\t\033[1m -h | help \033[0m this usage \n"
    printf "\t\033[1m menu if no options \033[0m\n" 
}

test_input_file()
{
   file=$1
   if [ "$file" == "" ]
       then 
       ret=$EXIT_ERROR
   fi

   if [ -f $file ] 
     then 
       echo Ok >/dev/null 
       ret=$EXIT_SUCCESS
     else
       ret=$EXIT_ERROR
   fi
}

ConvertFile()
{
    SaveCurrentWaitScreen
    if [ $ret -eq 0 ] 
        then 
            printLine "Hawk wait screen conversion ... "
            outfile=$MediaRep$MediaName
            $BIN_PATH/IVES_convert.ksh -i $inFile -o $outfile -s -g
            ret=$?
            if [ $ret -ne 0 ] 
                then 
                PrintFailed
            else 
                PrintOK
            fi
        else
            PrintFailed
    fi        
}

SaveCurrentWaitScreen()
{
   if [ -f $MediaRep$MediaName ] 
     then 
       printLine "Save current wait screen ... "
       cp -f $MediaRep$MediaName $MediaRep$SaveMediaName
       ret=$?
       if [ $ret -ne 0 ] 
           then 
           PrintFailed
       else 
           PrintOK
       fi
   fi
}

RestorCurrentWaitScreen()
{
    printLine "Save current wait screen ... "
    cp -f $MediaRep$MediaName $MediaRep$TmpMediaName
    ret=$?
    if [ $ret -ne 0 ] 
        then PrintFailed
    else 
        PrintOK
        printLine "Restor old wait screen ... "
        cp -f $MediaRep$SaveMediaName $MediaRep$MediaName
        ret=$?
        if [ $ret -ne 0 ] 
            then PrintFailed
        else 
            mv $MediaRep$TmpMediaName $MediaRep$SaveMediaName
            ret=$?
            if [ $ret -ne 0 ] 
                then PrintFailed
            else 
                PrintOK
            fi
        fi
    fi
}

EnterFileName()
{
    printf "Enter pathname of new wait screen file :"
    read inFile
}

menu()
{
    while [ 1 == 1 ]
    do
      clear
      printf "=========================================\n"
      printf "         Hawk wait screen menu \n"
      printf "=========================================\n"
      printf " 1 - Convert and install new wait screen \n"
      printf " 2 - Restor old wait screen \n"
      printf " 3 - Exit \n"
      printf "=========================================\n"
      printf " Enter your choice :"
      read answer
      case "$answer" in
          1)
          EnterFileName
          test_input_file $inFile
          if [ "$ret" -eq "$EXIT_ERROR" ]
              then        
              printf "\033[31m Error file $inFile not found \033[0m\n"
              else
              ConvertFile
              if [ "$ret" -eq "$EXIT_ERROR" ]
                  then        
                  printf "\033[31m waitscreen update $1 failed \033[0m\n"
              fi
          fi
          ;;
          2)
          RestorCurrentWaitScreen
          if [ "$ret" -eq "$EXIT_ERROR" ]
              then        
              printf "\033[31m waitscreen rollback failed \033[0m\n"
          fi
          ;;
          3) 
          exit EXIT_SUCCESS
          ;;
          *)
          printf "\033[31m Choice = 1, 2 or 3 \033[0m \n";;
      esac
      printf "press enter to conyinue\n"
      read key
   done
}



# =============================================================================
# main : parse args and exeute 
# =============================================================================
if [ "$1" == "" ]
    then menu
else
    while [ "$1" ] 
      do    
      case "$1" in
          -i)
          shift
          inFile=$1
          ;;
          -h|help)
          usage $0
          exit EXIT_SUCCESS
          ;;
          *)
          echo "Unknown command: $1"
          usage $0
          exit EXIT_ERROR
          ;;
      esac
      shift
    done
    test_input_file $inFile
    ConvertFile
fi
