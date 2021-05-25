#include "medkit/text.h"
#include "medkit/red.h"
#include "medkit/log.h"

RTPRedundantPayload::RTPRedundantPayload(BYTE *data,DWORD size)
{
	//NO primary data yet
	primaryCodec = 0;
	primaryType = 0;
	primaryData = NULL;
	primarySize = 0;
	
	if ( data != NULL && size > 0 ) ParseRed(data, size); 
}

void RTPRedundantPayload::ParseRed(BYTE *data,DWORD size)
{
	//Number of bytes to skip of text until primary data
	WORD skip = 0;

	//The the payload
	BYTE *payload = data;

	//redundant counter
	WORD i = 0;

	//Check if it is the last
	bool last = !(payload[i]>>7);

	//Read redundant headers
	while(!last)
	{
		//Check it
		/*
		    0                   1                    2                   3
		    0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		   |F|   block PT  |  timestamp offset         |   block length    |
		   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		   F: 1 bit First bit in header indicates whether another header block
		       follows.  If 1 further header blocks follow, if 0 this is the
		       last header block.

		   block PT: 7 bits RTP payload type for this block.

		   timestamp offset:  14 bits Unsigned offset of timestamp of this block
		       relative to timestamp given in RTP header.  The use of an unsigned
		       offset implies that redundant payload must be sent after the primary
		       payload, and is hence a time to be subtracted from the current
		       timestamp to determine the timestamp of the payload for which this
		       block is the redundancy.

		   block length:  10 bits Length in bytes of the corresponding payload
		       block excluding header.

		 */
		
		//Get Type
		BYTE type = payload[i++] & 0x7F;
		//Get offset
		WORD offset = payload[i++];
		offset = offset << 6 | payload[i]>>2;
		//Get size
		WORD sz = payload[i++] & 0x03;
		sz = (sz << 8) | ((WORD) payload[i++]);
		//Append new red header
		headers.push_back(RedHeader(type,offset,skip,sz));
		//Skip the redundant payload
		skip += sz;
		//Check if it is the last
		last = !(payload[i]>>7);
	}
	//Get primaty type
	primaryType = payload[i] & 0x7F;
	//Skip it
	i++;
	//Get redundant payload
	redundantData = payload+i;
	//Get prymary payload
	primaryData = redundantData+skip;
	//Get size of primary payload
	primarySize = size-i-skip;
}

RTPRedundantEncoder::RTPRedundantEncoder(BYTE ptype)
{
	this->ptype = ptype;
	redFrame = new TextFrame(true);
	redFrame->Alloc(1400);
	idle = true;
}

static BYTE BOMUTF8[]			= {0xEF,0xBB,0xBF};

void RTPRedundantEncoder::Encode(MediaFrame * frame)
{
    BYTE* red = (BYTE *) redFrame->GetData();
    //Init buffer length
    DWORD bufferLen = 0;
    
    //Fill with empty redundant packets
    for (int i=0;i<2-reds.size();i++)
    {
        //Empty t140 redundancy packet
        red[0] = 0x80 | ptype;
        //Randomize time
        red[1] = rand();
        red[2] = (rand() & 0x3F) << 2;
        //No size
        red[3] = 0;
        //Increase buffer
        red += 4;
        bufferLen += 4;
    }

    //Iterate to put the header blocks
    for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
    {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //Calculate diff
            DWORD diff = lastTime-f->GetTimeStamp();
            /****************************************************
             *  F: 1 bit First bit in header indicates whether another header block
             *     follows.  If 1 further header blocks follow, if 0 this is the
             *      last header block.
             *
             *  block PT: 7 bits RTP payload type for this block.
             *
             *  timestamp offset:  14 bits Unsigned offset of timestamp of this block
             *      relative to timestamp given in RTP header.  The use of an unsigned
             *      offset implies that redundant data must be sent after the primary
             *      data, and is hence a time to be subtracted from the current
             *      timestamp to determine the timestamp of the data for which this
             *      block is the redundancy.
             *
             *  block length:  10 bits Length in bytes of the corresponding data
             *      block excluding header.
             ********************************/
            red[0] = 0x80 | ptype;
            red[1] = diff >> 6;
            red[2] = (diff & 0x3F) << 2;
            red[2] |= f->GetLength() >> 8;
            red[3] = f->GetLength();
            //Increase buffer
            red += 4;
            bufferLen += 4;
    }
    //Set primary encoded data and last mark
    red[0] = ptype;
    //Increase buffer
    red++;
    bufferLen++;

    //Iterate to put the redundant data
    for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
    {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //Copy
            memcpy(red,f->GetData(),f->GetLength());
            //Increase sizes
            red += f->GetLength();
            bufferLen += f->GetLength();
    }

    //Check if there is frame
    if (frame)
    {
            //Copy
            memcpy(red,frame->GetData(),frame->GetLength());
            //Serialize data
			red += frame->GetLength();
            bufferLen += frame->GetLength();
            //Push frame to the redundancy queue
            reds.push_back(frame->Clone() );
    } else {
            //Push new empty frame
            reds.push_back(new TextFrame(lastTime,(wchar_t*)NULL,0));
    }
	
    //Check size of the queue
    if (reds.size()==3)
    {
            //Delete first
            delete(reds.front());
            //Dequeue
            reds.pop_front();
    }
	bool isNotNull=false;
	
    //Calculate timeouts
    if (frame)
    {
	    isNotNull=true;
            //Not idle anymore
            if ( frame->GetLength() != 0 && (frame->GetLength() != 3 || memcmp(frame->GetData(), BOMUTF8, 3) != 0) ) idle = false;
            lastTime = frame->GetTimeStamp();
    }
    else
    {
        int nbactivefr = 0;
		
        for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
        {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //If it is not empty
            if (f->GetLength())
            {
		isNotNull=true;
                if ( f->GetLength() == 3 && memcmp(f->GetData(), BOMUTF8, 3) == 0)
                {
			// BOM frame
                }
		else if ( f->GetLength() == 0)
		{
			// empty frame
		}
		else
                {
                    nbactivefr++;
                }
            }
        }

        idle = (nbactivefr == 0);
    }

	//Debug("Got red frame. idle=%d.\n", idle);
	redFrame->ClearRTPPacketizationInfo();
	//redFrame->SetLength(red - redFrame->GetData());
	redFrame->SetLength(bufferLen);
	redFrame->AddRtpPacket(0,redFrame->GetLength(),NULL,0,  idle && frame != NULL);
}

void RTPRedundantEncoder::EncodeBOM()
{
    TextFrame t(0, BOMUTF8, sizeof(BOMUTF8));
    Encode(&t);
}

MediaFrame * RTPRedundantEncoder::GetRedundantPayload()
{
	return redFrame;
}

RTPRedundantEncoder::~RTPRedundantEncoder()
{
    while ( reds.size() )
    {
        //Delete first
        delete(reds.front());
        //Dequeue
        reds.pop_front();
    }
	
	if (redFrame) delete redFrame;
}
