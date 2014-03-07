#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/options.h>
#include <astmedkit/frameutils.h>
#include "h263packetizer.h"

uint32_t rfc2190_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen)
{
	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"RFC2190 appending [%d:0x%.2x,0x%.2x]\n",bufferLen,buffer[0],buffer[1]);

	/* Check length */
	if (bufferLen<sizeof(struct RFC2190H263HeadersBasic))
		/* exit */
		return 0;

	/* Get headers */
	uint32_t x = ntohl(*(uint32_t *)buffer);

	/* Set headers */
	struct RFC2190H263HeadersBasic *headers = (struct RFC2190H263HeadersBasic *)&x;

	/* Get ini */
	uint8_t* in = buffer + sizeof(struct RFC2190H263HeadersBasic);
	uint32_t  len = sizeof(struct RFC2190H263HeadersBasic);

	if (headers->f)
	{
	  if (headers->p)
      /* If C type */
	  {
      if ( option_debug > 4 )
        ast_log(LOG_DEBUG,"RFC2190 C type %d\n",headers->f);

		  /* Skip rest of header */
		  in+=8;
		  len+=8;
	  }
	  else
      /* If B type */
	  {
      if ( option_debug > 4 )
        ast_log(LOG_DEBUG,"RFC2190 B type %d\n",headers->f);

		  /* Skip rest of header */
		  in+=4;
		  len+=4;
		}
	}
	else
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 A type %d\n",headers->f);
  }

	//ast_log(LOG_DEBUG,"RFC2190 len %d\n",len);

	if(headers->ebits!=0)
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 cut %d ebit (%d)\n", headers->ebits, bufferLen-len);
	  in[bufferLen-len-1] &= 0xff << headers->ebits;
	}	

	/* Mix first and end byte */
	if(headers->sbits!=0 && destLen>0)
	{
    if ( option_debug > 4 )
      ast_log(LOG_DEBUG,"RFC2190 mix %d sbit\n", headers->sbits);

		/* Append to previous byte */
    in[0] &= 0xff >> headers->sbits;		
		dest[destLen-1] |= in[0];
		/* Skip first */
		in++;
		len++;
	}	

	/* Copy the rest */
	memcpy(dest+destLen,in,bufferLen-len);	

  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"RFC2190 max write %d\n", destLen+bufferLen-len);

	/* Return added */
	return bufferLen-len;
}

void SendVideoFrameH263(struct ast_channel *chan, uint8_t *data, uint32_t size, int first, int last , int fps)
{
	uint8_t frameBuffer[PKT_SIZE];
	struct ast_frame *send = (struct ast_frame *) frameBuffer;
	uint8_t *frameData = NULL;
 	unsigned char RFC2190_header[4] = {0} ;
	
  if(data[0] == 0x00 && data[1] == 0x00 && (data[2] & 0xfc)==0x80){ /* PSC */
		/* RFC 2190 -5.1 Mode A
			0                   1                   2                   3
			0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			|F|P|SBIT |EBIT | SRC |I|U|S|A|R      |DBQ| TRB |    TR         |
			+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		
		SRC : 3 bits
		Source format, bit 6,7 and 8 in PTYPE defined by H.263 [4], specifies
		the resolution of the current picture.
		
		I:  1 bit.
		Picture coding type, bit 9 in PTYPE defined by H.263[4], "0" is
		intra-coded, "1" is inter-coded.
		*/
		
		// PDATA[4] ======> Bits 3-10 of PTYPE
		uint8_t format, pict_type;
		
		// Source Format = 4,5,6
		format = (data[4] & 0x3C)>>2;
		// Picture Coding Type = 7
		pict_type = (data[4] & 0x02)>>1;
		// RTP mode A header
    ((uint32_t *)RFC2190_header)[1] = ntohl( (format <<5) | (pict_type << 4) );
	}

  uint32_t *p = (uint32_t *) data;
  int gob_num = (ntohl(*p) >> 10) & 0x1f;
  char *dat = (char *)data;	
  
  static uint32_t tr = 0; //Static to have it when needed for splitting into multiple
  static uint32_t sz = 0; //packets (a hack that works for one video connection
  //         only, must be stored elsewhere for more)

	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"Send video frame [%p,%d,%d,%d,0x%.2x,0x%.2x,0x%.2x,0x%.2x]\n",send,size,first,last,data[0],data[1],data[2],data[3]);

	/* Check size */
	if (size+2>PKT_PAYLOAD)
	{
		/* Error */
		ast_log(LOG_ERROR,"Send video frame too large [%d]\n",size);
		/* Exit */
		return ;
	}

	/* clean */
	memset(send,0,PKT_SIZE);

	/* Set frame data */
	AST_FRAME_SET_BUFFER(send,send,PKT_OFFSET,0);
	/* Get the frame pointer */
	frameData = AST_FRAME_GET_BUFFER(send);


	/* Debug */
  if ( option_debug > 4 )
    ast_log(LOG_DEBUG,"GOB number %d\n",gob_num);

  if(gob_num == 0)
  {
    /* Get relevant framedata and memorize it for later use */
    /* Get the "temporal reference" from the H.263 frameheader. */
    tr = ((dat[2] & 0x03) * 64) + ((dat[3] & 0xfc) / 4);
    /* Get the Imgesize from the H.263 frameheader. */
    sz = (dat[4] >> 2) & 0x07;
  }
  else
  {
    /* The memorized values from the frame start will be used */
  }
  /* Construct payload header.
     Set videosize and the temporal reference to that of the frame */
  ((uint32_t *)RFC2190_header)[0] = ntohl((sz << 21) | (tr & 0x000000ff));
  
	/* Set frame len*/
	send->datalen = size+4;
	/* Set header */
	memcpy(frameData, RFC2190_header, 4);
	/* Copy */
	memcpy(frameData+4, data, size);
	/* Set timestamp */
	send->samples = 90000/fps;

	/* Set video type */
	send->frametype = AST_FRAME_VIDEO;
	/* Set codec value */
	send->subclass = AST_FORMAT_H263 | last;
	/* Rest of values*/
	send->src = "mp4play";
	send->delivery = ast_tv(0, 0);
	/* Don't free the frame outrside */
	send->mallocd = 0;


	/* Send */
	//vtc->channel->tech->write_video(vtc->channel, send);
	ast_write(chan, send);
}

