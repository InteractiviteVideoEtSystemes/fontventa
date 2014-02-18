#define H263P_HEADER_SIZE		2
#define H263_HEADER_MODE_A_SIZE 4
#define H263_HEADER_MODE_B_SIZE 8
#define H263_HEADER_MODE_C_SIZE 12

struct RFC2190H263HeadersBasic
{
        //F=0             F=1
        //P=0   I/P frame       I/P mode b
        //P=1   B frame         B fame mode C
        uint32_t trb:9;
        uint32_t tr:3;
        uint32_t dbq:2;
        uint32_t r:3;
        uint32_t a:1;
        uint32_t s:1;
        uint32_t u:1;
        uint32_t i:1;
        uint32_t src:3;
        uint32_t ebits:3;
        uint32_t sbits:3;
        uint32_t p:1;
        uint32_t f:1;
};

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



/* 3 zero bytes syncword */
static const uint8_t sync_bytes[] = { 0, 0, 0, 1 };


uint32_t h264_append(uint8_t *dest, uint32_t destLen, uint32_t destSize, uint8_t *buffer, uint32_t bufferLen)
{
	uint8_t nal_unit_type;
	unsigned int header_len;
	uint8_t nal_ref_idc;
	unsigned int nalu_size;

	uint32_t payload_len = bufferLen;
	uint8_t *payload = buffer;
	uint8_t *outdata = dest+destLen;
	uint32_t outsize = 0;


	/* +---------------+
	 * |0|1|2|3|4|5|6|7|
	 * +-+-+-+-+-+-+-+-+
	 * |F|NRI|  Type   |
	 * +---------------+
	 *
	 * F must be 0.
	 */
	nal_ref_idc = (payload[0] & 0x60) >> 5;
	nal_unit_type = payload[0] & 0x1f;

	/* at least one byte header with type */
	header_len = 1;

	ast_log(LOG_DEBUG, "h264 receiving %d bytes nal unit type %d\n", payload_len, nal_unit_type);

	switch (nal_unit_type) 
	{
		case 0:
		case 30:
		case 31:
			/* undefined */
			return 0;
		case 25:
			/* STAP-B		Single-time aggregation packet		 5.7.1 */
			/* 2 byte extra header for DON */
			/** Not supported */
			return 0;	
		case 24:
		{
			/**
			   Figure 7 presents an example of an RTP packet that contains an STAP-
			   A.  The STAP contains two single-time aggregation units, labeled as 1
			   and 2 in the figure.

			       0                   1                   2                   3
			       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                          RTP Header                           |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |STAP-A NAL HDR |         NALU 1 Size           | NALU 1 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 1 Data                           |
			      :                                                               :
			      +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |               | NALU 2 Size                   | NALU 2 HDR    |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                         NALU 2 Data                           |
			      :                                                               :
			      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			      |                               :...OPTIONAL RTP padding        |
			      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

			      Figure 7.  An example of an RTP packet including an STAP-A and two
					 single-time aggregation units
			*/
			ast_log(LOG_DEBUG, "STAP-A NAL\n");

			/* Skip STAP-A NAL HDR */
			payload++;
			payload_len--;
			
			/* STAP-A Single-time aggregation packet 5.7.1 */
			while (payload_len > 2) 
			{
				/* Get NALU size */
				nalu_size = (payload[0] << 8) | payload[1];

				ast_log(LOG_DEBUG, "STAP-A NAL of size %d %d\n", nalu_size, payload_len);

				/* strip NALU size */
				payload += 2;
				payload_len -= 2;

				if (nalu_size > payload_len)
					nalu_size = payload_len;

				outsize += nalu_size + sizeof (sync_bytes);

				/* Check size */
				if (outsize + destLen >destSize)
				{	
					ast_log(LOG_DEBUG, "Frame to small to add NAL [%d,%d,%d]\n",outsize,destLen,destSize);
					return 0;
				}

				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata += nalu_size;

				payload += nalu_size;
				payload_len -= nalu_size;
			}

			return outsize;
		}
		case 26:
			/* MTAP16 Multi-time aggregation packet	5.7.2 */
			header_len = 5;
			return 0;
			break;
		case 27:
			/* MTAP24 Multi-time aggregation packet	5.7.2 */
			header_len = 6;
			return 0;
			break;
		case 28:
		case 29:
		{
			/* FU-A	Fragmentation unit	 5.8 */
			/* FU-B	Fragmentation unit	 5.8 */
			uint8_t S, E;

			/* +---------------+
			 * |0|1|2|3|4|5|6|7|
			 * +-+-+-+-+-+-+-+-+
			 * |S|E|R| Type	   |
			 * +---------------+
			 *
			 * R is reserved and always 0
			 */
			S = (payload[1] & 0x80) == 0x80;
			E = (payload[1] & 0x40) == 0x40;

			ast_log(LOG_DEBUG, "S %d, E %d", S, E);

			if (S) 
			{
				/* NAL unit starts here */
				uint8_t nal_header;

				/* reconstruct NAL header */
				nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);

				/* strip type header, keep FU header, we'll reuse it to reconstruct
				 * the NAL header. */
				payload += 1;
				payload_len -= 1;

				nalu_size = payload_len;
				outsize = nalu_size + sizeof (sync_bytes);
				memcpy (outdata, sync_bytes, sizeof (sync_bytes));
				outdata += sizeof (sync_bytes);
				memcpy (outdata, payload, nalu_size);
				outdata[0] = nal_header;
				outdata += nalu_size;
				return outsize;

			} else {
				/* strip off FU indicator and FU header bytes */
				payload += 2;
				payload_len -= 2;

				outsize = payload_len;
				memcpy (outdata, payload, outsize);
				outdata += nalu_size;
				return outsize;
			}

			/* if NAL unit ends, flush the adapter */
			if (E) 
			{
				ast_log(LOG_DEBUG, "output %d bytes", outsize);

				return 0;
			}

			return outsize;
			break;
		}
		default:
		{
			/*rtph264depay->wait_start = FALSE;*/

			/* 1-23	 NAL unit	Single NAL unit packet per H.264	 5.6 */
			/* the entire payload is the output buffer */
			nalu_size = payload_len;
			outsize = nalu_size + sizeof (sync_bytes);
			memcpy (outdata, sync_bytes, sizeof (sync_bytes));
			outdata += sizeof (sync_bytes);
			memcpy (outdata, payload, nalu_size);
			outdata += nalu_size;

			return outsize;
		}
	}

	return 0;
}



uint32_t mpeg4_append(uint8_t *dest, uint32_t destLen, uint8_t *buffer, uint32_t bufferLen)
{
	/* Just copy */
	memcpy(dest+destLen,buffer,bufferLen);
	/* Return added */
	return bufferLen;
}
