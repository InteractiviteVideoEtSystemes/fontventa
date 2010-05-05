

/*===========================================================================*/
/* Unpublished Confidential Information of IVES. Do not disclose.            */
/* Copyright (c) 2003-2010 IVES All Rights Reserved.                         */
/*---------------------------------------------------------------------------*/
/*!
 * COMPANY   IVES
 *
 * MODULE    asterisk
 *
 * \author   Philippe Verney
 *
 * \file     astlog.c
 *
 * \brief    log parser
 *
 * \version  $Revision: 1.86 $
 *
 * \date     $Date: 2009/01/12 09:03:52 $
 *
 * \remarks
 *
 *---------------------------------------------------------------------------
 * gcc -o astlog astlog.c
 *
 *===========================================================================*/
// -----1=0-------2=0-------3=0-------4=0-------5=0-------6=0-------7=0-------8

/* ==========================================================================*/
/* File identification                                                       */
/* ==========================================================================*/
#ident "@(#) FERMA $Id: SIO_main.c,v 1.86 2009/01/12 09:03:52 pverney Exp $"

/* ==========================================================================*/
/* System include(s)                                                         */
/* ==========================================================================*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>

/* ==========================================================================*/
/* Specific include(s)                                                       */
/* ==========================================================================*/


/* ==========================================================================*/
/* Macros                                                                    */
/* ==========================================================================*/
#ifndef true 
#define true 1==1
#endif

#ifndef false 
#define false 1==0 
#endif

/* ==========================================================================*/
/* Constantes                                                                */
/* ==========================================================================*/
#define DEFAULT_FILE "/var/log/asterisk/messages"
#define EXIT_SUCESS 0
#define EXIT_FAILED 1
#define SIZE_BUFF   4096 

#define  CLS()            printf("\33[2J")
#define  STANDARD_VIDEO() printf("\033[0m")
#define  BOLD()           printf("\033[1m")
#define  NORMAL()         printf("\033[0m")
#define  REVERSE_VIDEO()  printf("\033[7m")
#define  bold "\33[1m"
#define  std  "\033[0m"

/* ========================================================================= */
/* Variables global                                                          */
/* ========================================================================= */
static char IvesInfo[]="@(#) $IVES: INFO Module astlog Ver $Name:  $ Archive time $Date: 2009/01/12 09:03:52 \n$ Compil date "  __DATE__ " $";

char          line[SIZE_BUFF];
char*         l_beg = &line[0] ;
char*         l_end = &line[0] ;
size_t        rd = 0 ;
size_t        analyse = 0 ;
unsigned int  count = 0 ;
int           fd_out = 0 ;
char          mark[PATH_MAX];
static const char * COL_color[] =
{
"\033[0m",        /* COL_NORMAL               */
"\033[30m",       /* COL_BLACK                */
"\033[31m",       /* COL_RED                  */
"\033[32m",       /* COL_GREEN                */
"\033[33m",       /* COL_YELLOW               */
"\033[34m",       /* COL_BLUE                 */
"\033[35m",       /* COL_MAGENTA              */
"\033[36m",       /* COL_CYAN                 */
"\033[37m",       /* COL_WHITE                */
"\033[1m",        /* COL_BOLD                 */
"\033[7m",        /* COL_REVERSE_VIDEO        */
"\033[4m",        /* COL_UNDERLINE            */
"\033[2m",        /* COL_SHADED               */
"\033[1m\033[4m", /* COL_BOLD_UNDERLINE       */
"\033[2m\033[4m", /* COL_SHADED_UNDERLINE     */
"\033[1m\033[7m", /* COL_BOLD_REVERSE_VIDEO   */
"\033[2m\033[7m"  /* COL_SHADED_REVERSE_VIDEO */
};


/**
 * Available colors.
 */
typedef enum
    {
        COL_NORMAL = 0,
        COL_BLACK,
        COL_RED,
        COL_GREEN,
        COL_YELLOW,
        COL_BLUE,
        COL_MAGENTA,
        COL_CYAN,
        COL_WHITE,
        COL_BOLD,
        COL_REVERSE_VIDEO,
        COL_UNDERLINE,
        COL_SHADED,
        COL_BOLD_UNDERLINE,
        COL_SHADED_UNDERLINE,
        COL_BOLD_REVERSE_VIDEO,
        COL_SHADED_REVERSE_VIDEO
    } ColorMode;


void usage(char* ProgramName)
{
    printf("%sNAME%s\n",bold,std);
    printf("  %s - Ives tools for asterisk log \n\n",
           ProgramName);
    printf("%sSYNOPSIS%s\n",bold,std);
    printf("\n%s  %s [-V] [-h] [-i input_file_name] [-m maker]  [-n number_line]%s\n",bold,ProgramName,std);
    printf("\n%sOPTIONS%s\n",bold,std);
    printf("  -V : Version\n");
    printf("  -i : Name of input file \n");
    printf("  -m : color line if string found \n");
    printf("  -n : Number of line already write \n");
    printf("\n%sDESCRIPTION%s\n",bold,std);
    printf("The %s%s%s is a viewer for log with color. \n",bold,ProgramName,std);    
    printf("\n%sSamples%s\n",bold,std);
    printf(" - To see traces ( mode tail ) from other file \n");
    printf(" astlog -i /tmp/log.log   \n");
    printf(" - To see traces ( mode tail ) from other file with first n line \n");
    printf(" astlog -i /tmp/log.log  -n 20 \n");
    exit(EXIT_SUCCESS);
}


/*
 * \brief Analyse les argumets,
 * fait les initialisations et lance la boucle de traitement principale
 *
 * \param argc nombre d'arguments
 * \param **argv liste des arguments
 *
 * \return rien
 **/
int main(int argc, char **argv)
{
  char             fileIn[PATH_MAX];
  char             fileOut[PATH_MAX];
  int              Status = EXIT_SUCCESS ;
  int              option;
  int              nbLine = 0 ;
    
  strncpy(fileIn,(const char*) DEFAULT_FILE , PATH_MAX );
  memset(mark,0,PATH_MAX);
    
  while ( (option=getopt(argc, argv, "?hVi:n:m:")) != EOF)
  {
    switch ( (char) option )
    {
      case '?':
      case 'h':
        usage( basename( argv[0]) );
        break;
                
      case 'V':
        printf( "\n%s\n\n", &IvesInfo[6] );
        exit( EXIT_SUCCESS );
        break;
                
      case 'i':
        strncpy(fileIn,optarg,PATH_MAX);
        break;
                
      case 'n':
        nbLine=atoi(optarg);
        break ;

      case 'm':
        strncpy(mark,optarg,PATH_MAX);
        break ;

      default :
        break;
    }
  }
  WaitLine(fileIn,nbLine);
  exit ( EXIT_SUCCESS );
}

int WaitLine( char* fileIn ,int nbLine) 
{
    int            fd   = 0 ;
    fd_set         ens ;
    int            nbFd = 0 ;
    struct timeval att ;
    int            i_desc ;
    char           buff[SIZE_BUFF];
    off_t          total = 0 ;
    off_t          rewind = nbLine * PATH_MAX ;
    
    FD_ZERO( & ens ) ;
    att.tv_sec = 1 ;
    
    fd =  open ( fileIn , O_RDONLY ) ;
    if ( fd == -1 )
    {
        fprintf (stderr , "open file %s , failed :%s \n",fileIn , strerror(errno) );
        return EXIT_FAILED ;
    }
    total = lseek ( fd , 0L , SEEK_END );
    lseek ( fd , 0L , SEEK_SET );
    if ( rewind < total )
        lseek ( fd , (total - rewind) , SEEK_SET );
    else
    {
        fprintf (stderr , "seek failed  \n",fileIn , strerror(errno) );
        lseek ( fd , 0L , SEEK_SET );
    }
    
    FD_SET( fd , & ens );
    while ( true )
    {
        if ( select ( 1 , &ens , NULL , NULL , & att ) != -1 )
        {
            memset(buff , 0 , SIZE_BUFF );
            rd = read ( fd , buff , SIZE_BUFF ); 
            while ( rd ) 
            {
                count ++ ;
                PrintLine( buff ) ; 
                memset(buff , 0 , SIZE_BUFF );
                rd = read ( fd , buff , SIZE_BUFF );
            }
        }
        sleep(1);
    }
}

int PrintLine(char* buff )
{
    char  lBuff[SIZE_BUFF];
    char* b_begin =  &buff[0] ;
    char* b_end   =  &buff[0] ;
    size_t   sz   =  0 ;
    // find first \n
    analyse = 0 ;
    while ( analyse < rd ) 
    {
        // Debug 
        // printf ( "b_begin= 0x%X, b_end = 0x%X total : %d \n", b_begin , b_end , count ); 
        b_end = strstr( b_begin , "\n" );
        if  ( b_end == NULL )
        {
            sz =  rd - analyse  ;
            // fprintf ( stderr , "EOL not found in %s\n",b_begin);
            if ( (size_t)(l_end - l_beg + sz ) < SIZE_BUFF )
            { 
                // sav begin 
                memcpy( l_end , b_begin , sz ) ;
                // fprintf ( stderr , "sav begin %s \n",line);
                l_end += sz ; 
            }
            else
            { 
                fprintf ( stderr , "Buff to small \n");
            }
        }
        else
        { 
            sz =  b_end - b_begin +1 ;
            memcpy ( l_end , b_begin , sz );
            select_util () ;
            l_beg = &line[0] ;
            l_end = &line[0] ;
        }
        b_begin += sz ;
        analyse += sz ; 
    }
    fflush(NULL) ;
}

int select_util() 
{
    if (  strcasestr ( line , "ERRO" ) != NULL )
      fprintf ( stdout , "%s%s%s" , COL_color[COL_RED], line , COL_color[COL_NORMAL]);
    else if (  strcasestr ( line , "WARN " ) != NULL  ||   
               strcasestr ( line , "WARNING") != NULL )
      fprintf ( stdout , "%s%s%s%s" , COL_color[COL_REVERSE_VIDEO] ,COL_color[COL_MAGENTA], line , COL_color[COL_NORMAL]);
    else if (  strcasestr ( line , "NOTICE" ) != NULL )
      fprintf ( stdout , "%s%s%s" , COL_color[COL_GREEN], line , COL_color[COL_NORMAL]);
    else if (  strcasestr ( line , "EVENT" ) != NULL )
      fprintf ( stdout , "%s%s%s" , COL_color[COL_BLUE], line , COL_color[COL_NORMAL]);
    else if (  strcasestr ( line , mark ) != NULL )
      fprintf ( stdout , "%s%s%s" , COL_color[COL_CYAN], line , COL_color[COL_NORMAL]);
    else 
      fprintf ( stdout , "%s" , line );


  memset ( line , 0 , SIZE_BUFF ) ;
}
