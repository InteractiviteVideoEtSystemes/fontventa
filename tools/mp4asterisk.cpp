/* mp4asterisk
 * Video bandwith control for mp4
 * Copyright (C) 2006 Sergio Garcia Murillo
 *
 * sergio.garcia@fontventa.com
 * http://sip.fontventa.com
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <mp4v2/mp4v2.h>


static void dump_buffer_hex(unsigned char * text, unsigned char * buff, int len)
{
        int i,j;
        char *temp;
        temp = (char*)malloc(10*9+20);
        if (temp == NULL) {
                printf("dump_buffer_hex: failed to allocate buffer!\n");
                return;
        }
        printf("hex : (%d) %s\n",len, text);
        j=0;
        for (i=0;i<len;i++) {
                sprintf( temp+(9*j), "%04d %02x  ", i, buff[i]);
                j++;
                if (j==10)
                {
                 printf("hex : %s\n",temp);
                 j=0;
                }
        }
        if (j!=0)
        printf("hex : %s\n",temp);
        free(temp);
}

int mp4asterisk(char *name)
{
  int index;
  int namelen;
	unsigned char type2; 
	MP4TrackId hintId;
	MP4TrackId trackId;
	const char *type = NULL;	
 	unsigned short numHintSamples;
 	unsigned short packetIndex;
	
	u_int32_t datalen;
	u_int8_t databuffer[8000];
  uint8_t* data;	
  
  int outfile;

	char filename[8000];
  	
  strcpy(filename, name);  	
	namelen=strlen(name);
		
	if ((strcmp(name+namelen-3, "mp4")) 
   && (strcmp(name+namelen-3, "3gp")) 
   && (strcmp(name+namelen-3, "mov"))) 
	{
		printf("mp4asterisk\nusage: mp4asterisk file\n");
		printf("invalide file format : %s\n", name+namelen-3);
		return -1;
	}
  		
	/* Open mp4*/
	MP4FileHandle mp4 = MP4Read(name);

	/* Disable Verbosity */
	//MP4SetVerbosity(mp4, 0);
	
	index = 0;

	/* Find first hint track */
	hintId = MP4FindTrackId(mp4, index++, MP4_HINT_TRACK_TYPE, 0);

	/* If not found video track*/
	while (hintId!=MP4_INVALID_TRACK_ID)
	{
		unsigned int frameTotal;
		int timeScale;

		trackId = MP4GetHintTrackReferenceTrackId(mp4, hintId);
		
    /* Get type */
		type = MP4GetTrackType(mp4, trackId);		
		
		if (strcmp(type, MP4_VIDEO_TRACK_TYPE) == 0)
    { 

		/* Get video type */
		MP4GetHintTrackRtpPayload(mp4, hintId, &name, NULL, NULL, NULL);		

    if (name)
		printf("Track name: %s\n", name);
		else
		printf("Track name: (null)\n");
		
		timeScale = MP4GetTrackTimeScale(mp4, hintId);	

		printf("Track timescale: %d\n", timeScale);

    frameTotal = MP4GetTrackNumberOfSamples(mp4,hintId);
    
		printf("Number of samples: %d\n", frameTotal);

    if (name == NULL) // Vidiator
    //strcat(filename, ".h263");
    strcpy(filename+namelen-4, ".h263p");
    else    
    if (!strcmp(name, "H263-2000"))
    //strcat(filename, ".h263");
    strcpy(filename+namelen-4, ".h263p");
    else
    if (!strcmp(name, "H264"))
    //strcat(filename, ".h264");
    strcpy(filename+namelen-4, ".h264");
    else
    if (!strcmp(name, "H263"))
    //strcat(filename, ".h263");
    strcpy(filename+namelen-4, ".h263");
    else
    strcpy(filename, name);

		printf("Create file: %s\n", filename);

		outfile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666) ;
		if (outfile < 0)
		{
			fprintf(stderr, "Failed to create %s\n", filename) ;
			return 1;
		}

    if (name)
    if (!strcmp(name, "H264"))
    {
      uint8_t **seqheader, **pictheader;
      uint32_t *pictheadersize, *seqheadersize;
      uint32_t ix;
      
			int samples;
			int zero = 0;
      unsigned int ts;			  
			unsigned short len;
			int mark = 0x8000;      

      MP4GetTrackH264SeqPictHeaders(mp4, trackId, 
				&seqheader, &seqheadersize,
				&pictheader, &pictheadersize);  
        
      for (ix = 0; seqheadersize[ix] != 0; ix++)
      {
        //dump_buffer_hex("SeqHeader", seqheader[ix], seqheadersize[ix]);
 
        memcpy(databuffer, seqheader[ix], seqheadersize[ix]);
				  datalen = seqheadersize[ix];

        ts = htonl(zero);
        len = htons(datalen | mark);			   
			   
		    dump_buffer_hex((unsigned char *)name, (unsigned char *)databuffer, datalen);
			   
 		  	write(outfile, &ts, 4) ;
	      write(outfile, &len, 2) ;
	      write(outfile, databuffer, datalen) ;
      }
      for (ix = 0; pictheadersize[ix] != 0; ix++)
      {
        //dump_buffer_hex("PictHeader", pictheader[ix], pictheadersize[ix]);

        memcpy(databuffer, pictheader[ix], pictheadersize[ix]);
				  datalen = pictheadersize[ix];
	
	      ts = htonl(zero);
        len = htons(datalen | mark);

		    dump_buffer_hex((unsigned char *)name, (unsigned char *)databuffer, datalen);

 		  	write(outfile, &ts, 4) ;
	      write(outfile, &len, 2) ;
	      write(outfile, databuffer, datalen) ;
      }           
    }

		/* Iterate frames */
		for (int i=1;i<frameTotal+1;i++)
		//for (int i=1;i<10+1;i++)
		{
		
		  if (!MP4ReadRtpHint(mp4, hintId, i, &numHintSamples)) {
			  printf("MP4ReadRtpHint failed [%d,%d]\n", hintId, i);
		  }

		  //printf("Hint samples: %d\n", numHintSamples);
		
			/* Get duration of sample */
		  unsigned int frameDuration = MP4GetSampleDuration(mp4, hintId, i);		
		
			/* Get size of sample */
			unsigned int frameSize = MP4GetSampleSize(mp4, hintId, i);

			/* Get sample timestamp */
			unsigned int frameTime = MP4GetSampleTime(mp4, hintId, i);

			printf("%d\t%d\t%d\t%d\t%d\n",i,frameDuration, frameTime,frameSize,frameSize*8/10);
			
			for (int j=0;j<numHintSamples;j++)
			{
			  int samples = frameDuration * (90000 / timeScale);
			  int zero = 0;
       	unsigned int ts;			  
			  unsigned short len;
			  int mark;
			  
			  printf(" Packet index %d, samples %d\n",j, samples);

        data = databuffer;

        /* Read next rtp packet */
	       if (!MP4ReadRtpPacket(mp4, hintId, j,
        (u_int8_t **)&data,
				(u_int32_t *)&datalen,
				0, 0,	1)) 
        {
		      printf("Error reading packet [%d,%d]\n", hintId, trackId);
		    }
		    else
		    {
		      //dump_buffer_hex((unsigned char *)name, (unsigned char *)databuffer, datalen);
        }
	
  		  //printf(" Data Header + data length : %d\n", datalen);  		  

        if (j==(numHintSamples-1))
        mark = 0x8000;
        else
        mark = 0;
  
        ts = htonl(samples);
        len = htons(datalen | mark);
  		   
  		  //if (j==0)
 		  	write(outfile, &ts, 4) ;
        //else					
 		  	//write(outfile, &zero, 4) ; 	
	      write(outfile, &len, 2) ;
	      write(outfile, databuffer, datalen) ;

		  }
			
		}
		
	  close(outfile);		
		
		}
		
		/* Get the next hint track */
		hintId = MP4FindTrackId(mp4, index++, MP4_HINT_TRACK_TYPE, 0);		
	}

	/* Close */
	MP4Close(mp4);

	/* End */
	return 0;
}


#if defined (_LITTLE_ENDIAN) || defined (__LITTLE_ENDIAN)

/* 
	Macros to convert from Intel little endian data structures to this machine's
	and vice versa.
 */
#define L2H_SHORT(from, to)  *((short *)(to)) = *((short *)(from))
#define H2L_SHORT(from, to)  *((short *)(to)) = *((short *)(from))
#define L2H_LONG(from, to)   *((long *)(to)) = *((long *)(from))
#define H2L_LONG(from, to)   *((long *)(to)) = *((long *)(from))

/* 
	Macros to convert from Sun big endian data structures to this machine's
	and vice versa.
 */
#define B2H_SHORT(from, to) \
	((char *)(to))[0] = ((char *)(from))[1];	\
	((char *)(to))[1] = ((char *)(from))[0];
#define H2B_SHORT(from, to) \
	((char *)(to))[0] = ((char *)(from))[1];	\
	((char *)(to))[1] = ((char *)(from))[0];

#define B2H_LONG(from, to) \
	((char *)(to))[0] = ((char *)(from))[3];	\
	((char *)(to))[1] = ((char *)(from))[2];	\
	((char *)(to))[2] = ((char *)(from))[1];	\
	((char *)(to))[3] = ((char *)(from))[0];
#define H2B_LONG(from, to) \
	((char *)(to))[0] = ((char *)(from))[3];	\
	((char *)(to))[1] = ((char *)(from))[2];	\
	((char *)(to))[2] = ((char *)(from))[1];	\
	((char *)(to))[3] = ((char *)(from))[0];

#else /* Must be _BIG_ENDIAN */

#define L2H_SHORT(from, to) \
	((char *)(to))[0] = ((char *)(from))[1];	\
	((char *)(to))[1] = ((char *)(from))[0];
#define H2L_SHORT(from, to) \
	((char *)(to))[0] = ((char *)(from))[1];	\
	((char *)(to))[1] = ((char *)(from))[0];

#define L2H_LONG(from, to) \
	((char *)(to))[0] = ((char *)(from))[3];	\
	((char *)(to))[1] = ((char *)(from))[2];	\
	((char *)(to))[2] = ((char *)(from))[1];	\
	((char *)(to))[3] = ((char *)(from))[0];
#define H2L_LONG(from, to) \
	((char *)(to))[0] = ((char *)(from))[3];	\
	((char *)(to))[1] = ((char *)(from))[2];	\
	((char *)(to))[2] = ((char *)(from))[1];	\
	((char *)(to))[3] = ((char *)(from))[0];

#define B2H_SHORT(from, to)  *((short *)(to)) = *((short *)(from))
#define H2B_SHORT(from, to)  *((short *)(to)) = *((short *)(from))

#define B2H_LONG(from, to)   *((long *)(to)) = *((long *)(from))
#define H2B_LONG(from, to)   *((long *)(to)) = *((long *)(from))

#endif /*big-endian*/



int asteriskmp4(char *name)
{
  int index;
  int namelen;
	unsigned char type2; 
	MP4TrackId hintId;
	MP4TrackId trackId;
	const char *type = NULL;	
 	unsigned short numHintSamples;
 	unsigned short packetIndex;
	
	u_int32_t datalen;
	u_int8_t databuffer[8000];
  uint8_t* data;	
  
  int infile;

	char filename[8000];
  	
  int loop=1;
  
  char str[4];
	unsigned short packet_size;
	unsigned long packet_delta;
	unsigned long image_delta = 0;
	int mark = 0;
  unsigned char h263_header[4];
	unsigned char h263_data[2*1024];
	unsigned long h263_packet = 0;
	unsigned long h263_size;  
	unsigned long time = 0;
  	
	namelen=strlen(name);
		
	if ((strcmp(name+namelen-5, ".h263")) 
   && (strcmp(name+namelen-5, "3gp")) 
   && (strcmp(name+namelen-5, "mov"))) 
	{
		printf("mp4asterisk\nusage: mp4asterisk file\n");
		printf("invalide file format : %s\n", name+namelen-3);
		return -1;
	}

    
	infile = open(name, O_RDONLY, 0);
	if (infile < 0)
	{
		close(infile);
		fprintf(stderr, "Failed to open asterisk input file %s\n", name) ;
		return -1;				
  }	

  strcpy(filename, name);
  strcpy(filename+namelen-5, ".mp4");
  		
	/* Open mp4*/
	MP4FileHandle mp4 = MP4Create(filename, 9);

	/* Disable Verbosity */
	//MP4SetVerbosity(mp4, 0);
	
	index = 0;
		
	while (loop)
	{
		/* read delta_t */
		if (read(infile, str, 4) <= 0)
		{
			fprintf(stderr, "Failed to read delta_t\n") ;
			loop = 0;
			break ;
		}
				
    B2H_LONG(str, &packet_delta);
 		printf("Video frame delta #%ld:\n", packet_delta);		
 			
 			if (image_delta == 0)
 			image_delta = packet_delta;

		/* read packet size */
		if (read(infile, str, 2) < 0)
		{
			fprintf(stderr, "Failed to read packet size\n") ;
			return -1;
		}
		B2H_SHORT(str, &packet_size);
 		printf("Video frame length #%d:\n", packet_size);		
 			
		if (packet_size & 0x8000) {
			mark = 1;
		}
		else {
			mark = 0;
		}
   	packet_size &= 0x7fff;
	
    /* read h263 header */
		if (read(infile, h263_header, 4) < 0)
		{
			fprintf(stderr, "Failed to read h263 header\n") ;
			return -1;
		}

		/* read video frame */
		h263_size = packet_size - 4;
		if (read(infile, h263_data, h263_size) > 0)
		{
			h263_packet ++;
		}
		else
		{
			fprintf(stderr, "Failed to read video frame\n") ;
			return -1;
		}

		if (mark)
    {
   		printf("Video image delta #%ld:\n", image_delta);		
			time += image_delta;
			//time += (image_delta * 1000)/90000;
			image_delta = 0;
			printf("Video_time = %ld\n", time);
			printf("Video_time = %ld\n", (time)/90);
											
			//break;
		}
	}	
	
	close(infile);

	/* Close */
	MP4Close(mp4);
	
	return 0;
}





int main(int argc,char **argv)
{
  int index;
	char *name;  
  int namelen;
	unsigned char type2; 
	MP4TrackId hintId;
	MP4TrackId trackId;
	const char *type = NULL;	
 	unsigned short numHintSamples;
 	unsigned short packetIndex;
	
	u_int32_t datalen;
	u_int8_t databuffer[8000];
  uint8_t* data;	
  
  int outfile;

	char filename[8000];
  	
	/* Check args */
	if (argc<2)
	{
		printf("mp4asterisk\nusage: mp4asterisk file\n");
		return -1;
	}
	
	name=argv[1];
	namelen=strlen(name);
	
	if (strlen(name) < namelen)
	{
		printf("mp4asterisk\nusage: mp4asterisk file\n");
		return -1;
	}
	
	if ((!strcmp(name+namelen-4, "h263")) || (!strcmp(name+namelen-4, "h264"))) 
	{
		return asteriskmp4(name);
	}
	else
	if (!strcmp(name+namelen-3, "mov")) 
	{
		return mp4asterisk(name);
	}
	else
	if ((!strcmp(name+namelen-3, "mp4")) || (!strcmp(name+namelen-3, "3gp"))) 
	{
		return mp4asterisk(name);
	}
	else
	{
		printf("mp4asterisk\nusage: mp4asterisk file\n");
		printf("invalide file format : %s\n", name+namelen-3);
		return -1;
	}  		

	/* End */
	return 0;
}
