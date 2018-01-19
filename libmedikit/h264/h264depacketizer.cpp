/* 
 * File:   h264depacketizer.cpp
 * Author: Sergio
 * 
 * Created on 26 de enero de 2012, 9:46
 */

#include "h264depacketizer.h"
#include "../medkit/codecs.h"
#include "../medkit/log.h"

/* 3 zero bytes syncword */
static uint8_t sync_bytes[] = { 0, 0, 0, 1 };


H264Depacketizer::H264Depacketizer() : frame(VideoCodec::H264,0)
{
	useStartCode = false;
	hasPPS = false;
	hasSPS = false;
	frame.SetIntra(false);
}

H264Depacketizer::~H264Depacketizer()
{

}
void H264Depacketizer::SetTimestamp(DWORD timestamp)
{
	//Set timestamp
	frame.SetTimestamp(timestamp);
}
void H264Depacketizer::ResetFrame()
{
	//Clear packetization info
	frame.ClearRTPPacketizationInfo();
	//Reset
	memset(frame.GetData(),0,frame.GetMaxMediaLength());
	//Clear length
	frame.SetLength(0);
	frame.SetIntra(false);
	hasPPS = false;
	hasSPS = false;
}

MediaFrame* H264Depacketizer::AddPayload(BYTE* payload, DWORD payload_len, bool mark)
{
	BYTE nalHeader[4];
	BYTE nal_unit_type;
	BYTE nal_ref_idc;
	BYTE S, E;
	DWORD nalu_size;
	DWORD pos;
	//Check lenght
	if (!payload_len)
		//Exit
		return NULL;

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

	//printf("[NAL:%x,type:%x]\n", payload[0], nal_unit_type);

	//Check type
	switch (nal_unit_type)
	{
		case 0:
		case 30:
		case 31:
			/* undefined */
			return NULL;
		case 25:
			/* STAP-B		Single-time aggregation packet		 5.7.1 */
			/* 2 byte extra header for DON */
			/** Not supported */
			return NULL;
		case 24:
			//Log("H.264: STAP-A\n");
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
			//Everything goes to the payload
			frame.AddRtpPacket(0,0,payload,payload_len, mark);

			/* Skip STAP-A NAL HDR */
			payload++;
			payload_len--;

			/* STAP-A Single-time aggregation packet 5.7.1 */
			while (payload_len > 2)
			{
				/* Get NALU size */
				nalu_size = (payload[0] << 8) | payload[1];

				/* strip NALU size */
				payload += 2;
				payload_len -= 2;

				//Get nal type
				BYTE nalType = payload[0] & 0x1f;
				//Check it
				if (nalType==0x05)
					//It is intra
					if (hasPPS && hasSPS) frame.SetIntra(true);;

				if (useStartCode)
				{
					// Add start code to mark NALU beginnging
					frame.AppendMedia(sync_bytes, sizeof (sync_bytes));
				}
				else
				{
					// Store nalu size before nalu data (for MP4) files
					set4(nalHeader,0,nalu_size);
					frame.AppendMedia(nalHeader, sizeof (nalHeader));
				}
				
				//Append NALU data
				frame.AppendMedia(payload,nalu_size);
				
				payload += nalu_size;
				payload_len -= nalu_size;
			}
			break;
		case 26:
			/* MTAP16 Multi-time aggregation packet	5.7.2 */
			return NULL;
		case 27:
			/* MTAP24 Multi-time aggregation packet	5.7.2 */
			return NULL;
		case 28:
		case 29:
			//Log("H.264: FUA-A\n");
			/* FU-A	Fragmentation unit	 5.8 */
			/* FU-B	Fragmentation unit	 5.8 */


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

			/* strip off FU indicator and FU header bytes */
			nalu_size = payload_len-2;

			if (S)
			{
				/* NAL unit starts here */
				BYTE nal_header;

				/* reconstruct NAL header */
				nal_header = (payload[0] & 0xe0) | (payload[1] & 0x1f);

				//Get nal type
				BYTE nalType = nal_header & 0x1f;
				//Check it
				if (nalType==0x05)
				{
					if ( !hasSPS || !hasPPS)
					{
						Log("H.264: I-frame but missing PPS or SPS Possible packetization issue.\n");
					}
					
					if (hasPPS && hasSPS) frame.SetIntra(true);

				}

				//Get init of the nal
				iniFragNALU = frame.GetLength();

				if (useStartCode)
				{
					// Add start code to mark NALU beginnging
					frame.AppendMedia(sync_bytes, sizeof (sync_bytes));
				}
				else
				{
					// add placeholder for NALU size
					set4(nalHeader,0,1);
					frame.AppendMedia(nalHeader, sizeof (nalHeader));
				}

				//Start with reconstructed NAL header
				frame.AppendMedia(&nal_header,1);
			}

			//Get position
			pos = frame.GetLength();
			//Append data
			frame.AppendMedia(payload+2,nalu_size);
			//Add rtp payload
			frame.AddRtpPacket(pos,nalu_size,payload,2,mark);

			if (E)
			{
				if ( !useStartCode )
				{
					//Get NAL size
					DWORD nalSize = frame.GetLength()-iniFragNALU-4;
					//store it before the NALU data
					set4(frame.GetData(),iniFragNALU,nalSize);
				}
				
				if (!mark) Log("H.264: warning end of FU-A and RTP mark is not set. Possible packetization issue.\n");
			}
			//Done
			break;
		default:
			/* 1-23	 NAL unit	Single NAL unit packet per H.264	 5.6 */
			//Check it
			//Log("H.264: single NAL\n");
			switch(nal_unit_type)
			{
				case 0x05: // Intraframe 
					if ( !hasSPS || !hasPPS)
					{
						Log("H.264: I-frame but missing PPS or SPS Possible packetization issue.\n");
					}
					if (hasPPS && hasSPS) frame.SetIntra(true);
					break;
				
				case 0x01: // P-frame
					if ( hasSPS || hasPPS)
					{
						Log("H.264: P-frame but has PPS or SPS Possible packetization issue.\n");
					}					
					break;

				case 0x07:
					Log("H.264: Got SPS\n");
					hasSPS = true;
					break;
					
				case 0x08:
					Log("H.264: Got PPP\n");
					hasPPS = true;
					break;
					
			}
			
			/* the entire payload is the output buffer */
			nalu_size = payload_len;
			if (useStartCode)
			{
				// Add start code to mark NALU beginnging
				frame.AppendMedia(sync_bytes, sizeof (sync_bytes));
			}
			else
			{
				// Store nalu size before nalu data (for MP4) files
				set4(nalHeader,0,nalu_size);
				frame.AppendMedia(nalHeader, sizeof (nalHeader));
			}

			//Get current position in frame
			DWORD pos = frame.GetLength();
			//And data
			frame.AppendMedia(payload, nalu_size);
			//Add RTP packet -IVES added sync
			//frame.AddRtpPacket(pos,nalu_size, sync_bytes, sizeof(sync_bytes), mark);
			frame.AddRtpPacket(pos,nalu_size, NULL, 0, mark);
			//Done
			break;
	}

	return &frame;
}

