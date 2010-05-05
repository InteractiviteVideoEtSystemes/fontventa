/* H324M library
 *
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
#include <iostream>
#include <fstream>
#include "H324CCSRLayer.h"
#include "crc16.h"
#include "log.h"

#define SRP_SRP_COMMAND 249
#define SRP_SRP_RESPONSE 251
#define SRP_NSRP_RESPONSE 247


H324CCSRLayer::H324CCSRLayer() : sdu(255),ccsrl(255)
{
	//Initialize variables
	lastsn = 0xFF;
	sentsn = 0;
	cmdsn = 0xFF;
	cmd = NULL;
	waiting = false;
	isPDU = false;

	std::fstream flog;

	flog.open ("h245.log",ios::out|ios::app);
	flog << "****\r\n-Init call\r\n";
	flog.close();

	//Begin stream encoding
	strm.BeginEncoding();
}

H324CCSRLayer::~H324CCSRLayer()
{
}

void H324CCSRLayer::Send(BYTE b)
{
	//Append byte to stream
	sdu.SetAt(sdu.GetSize(),b);
}

void H324CCSRLayer::SendClosingFlag()
{
	//Check minimum length
	if (sdu.GetSize()<3)
		return;

	//Log
	std::fstream flog;
	flog.open ("h245.log",ios::out|ios::app);
	flog << "-SendClosingFlag\r\n";
	//The header
	BYTE header = sdu[0];

	//The sequence number
	BYTE sn;

	//The last field
	BYTE lsField;

	//The crc
	CRC16 crc;

	//Feed sdu buffer to crc
	crc.Add(sdu.GetPointer(),sdu.GetSize()-2);

	//Calculate crc
	WORD crcA = crc.Calc();

	//Get sdu crc
	WORD crcB = (sdu[sdu.GetSize()-1] << 8) | sdu[sdu.GetSize()-2];

	//Check it's good crc
	if (crcA!=crcB)
	{
		flog << "BAD CRC\n";
		goto clean;
	}

	//Depending on the type
	switch(header)
	{
		case SRP_SRP_COMMAND:
			//Check minimum length
			if (sdu.GetSize()<5)
				goto clean;

			//And the sn
			sn = sdu[1];

			Logger::Debug("Received SRP_SRP_COMMAND [%d]\n",sn);
			flog << "SRP_SRP_COMMAND\n";
			//Send NSRP Response
			SendNSRP(sn);

			//Check for retransmission
			if (sn == lastsn)
			{
				flog << "Retransmission\n";
				goto clean;
			}

			//Update lastsn
			lastsn = sn;

			//Get he ccsrl header
			lsField = sdu[2];

			//Encue the sdu to the ccsrl stream
			ccsrl.Concatenate(PBYTEArray(sdu.GetPointer()+3,sdu.GetSize()-5));

			//If it's the last ccsrl sdu
			if (lsField)
			{
				//Decode
				H324ControlPDU pdu;
	
				//Decode
				while (!ccsrl.IsAtEnd() && pdu.Decode(ccsrl))
				{
					//Launch event
					OnControlPDU(pdu);
					
					//Byte aling the stream
					ccsrl.ByteAlign();

					//Log
					pdu.PrintOn(flog);
					flog << "\r\n";
				}

				//Reset the decoder just if something went wrong
				ccsrl.ResetDecoder();

				//Clean 
				ccsrl.SetSize(0);
			}
			break;
		case SRP_NSRP_RESPONSE:
			//Check nsrp field
			if (sdu[1]==cmdsn)
			{
				Logger::Debug("Received SRP_NSRP_RESPONSE [%d]\n",sdu[1]);
				flog << "SRP_NSRP_RESPONSE\n";
				//End waiting
				waiting = false;
			} else
				Logger::Debug("Received out of order SRP_NSRP_RESPONSE [%d]\n",sdu[1]);
			break;
		case SRP_SRP_RESPONSE:
			Logger::Debug("Received SRP_SRP_RESPONSE\n");
			flog << "SRP_RESPONSE\n";
			//End waiting
			waiting = false;
			break;
	}

clean:
	//Clean sdu
	sdu.SetSize(0);
	flog.close();
}


void H324CCSRLayer::SendNSRP(BYTE sn)
{
	Logger::Debug("Sending NSRP [%d]\n",sn);

	//The header
	BYTE header[2];

	//Set the type
	header[0] = SRP_NSRP_RESPONSE;
	header[1] = sn;

		//Create the crc
	CRC16 crc;

	//Add the crc
	crc.Add(header,2);

	//Get the crc
	WORD c = crc.Calc();
	
	//Create the SDU
	H223MuxSDU* rpl = new H223MuxSDU(header,2);

	//Add crc
	rpl->Push(((BYTE*)&c)[0]);
	rpl->Push(((BYTE*)&c)[1]);

	//Enqueue to the end list
	rpls.push_back(rpl);
}

void H324CCSRLayer::SendPDU(H324ControlPDU &pdu)
{
	//Encode pdu
	pdu.Encode(strm);
	//Set flag
	isPDU = true;

	Logger::Debug("Encode PDU [%d]\n",strm.GetSize());

	BuildCMD();
}

void H324CCSRLayer::BuildCMD()
{
	//If it's not empty
	if (!isPDU)
		return;

	//Finish encoding
	strm.CompleteEncoding();

	//Get the h245 encoded length
	int pduLen = strm.GetSize();
	int len = 0;
	int packetLen = 0;


	Logger::Debug("Sending CMD [%d,%d]\n",sentsn,pduLen);

	//CCSRL partitioning
	while (len<pduLen)
	{
		BYTE lsField;

		//Calculate next packet size
		if (pduLen-len>256)
		{
			//Not last packet
			packetLen = 256;
			lsField = 0x00;
		} else {
			//Last packet
			packetLen = pduLen-len;
			lsField = 0xFF;
		}

		//SRP header
		BYTE header[2];

		//Fill it
		header[0] = SRP_SRP_COMMAND;
		header[1] = sentsn++;

		//Create the SDU
		H223MuxSDU* cmd = new H223MuxSDU(header,2);

		//Calculate checksum
		CRC16 crc;

		//Add to crc
		crc.Add(header,2);

		//Append CCSRL header field
		cmd->Push(lsField);

		//Add to crc
		crc.Add(lsField);

		//Append payload to sdu
		cmd->Push(strm.GetPointer()+len,packetLen);

		//Append payload to crc
		crc.Add(strm.GetPointer()+len,packetLen);

		//Get the crc
		WORD c = crc.Calc();
	
		//Add crc
		cmd->Push(((BYTE*)&c)[0]);
		cmd->Push(((BYTE*)&c)[1]);

		//Enqueue the list
		cmds.push_back(cmd);

		//Increment length
		len +=packetLen;
	}

	//Clean stream
	strm.SetSize(0);

	//No cmd
	isPDU = false;

	//Begin encoding
	strm.BeginEncoding();
}

H223MuxSDU* H324CCSRLayer::GetNextPDU()
{
	//No cmd
	isCmd = false;

	//If we have any pending reply
	if(rpls.size()>0)
		return rpls.front();

	//It's a cmd
	isCmd = true;

	//Still waiting for reply?
	if (!waiting)
	{
		//Got response so delete
		if (cmd!=NULL)
		{
			//Delete
			delete cmd;
			//No comand
			cmd = NULL;
		}
		
		//If we don't have elements
		if (cmds.size()==0)
			return NULL;

		//Get first command
		cmd = cmds.front();

		//Remove
		cmds.pop_front();	

		//Update last sequence numbar
		cmdsn = (cmdsn+1)%256;

		//Sending cmd
		Logger::Debug("Sending CMD [%d] - %d left\n",cmdsn,cmds.size());
		{
			
			PPER_Stream aux;
			aux.Concatenate(PBYTEArray(cmd->GetPointer()+3,cmd->Length()-5));

			//Decode
			H324ControlPDU pdu;
			
			fstream log;
			//log.open ("c:\\logs\\h245.txt",ios::out|ios::app);
			log.open ("h245.log",ios::out|ios::app);
			log << "-Sending\n";
			//Decode
			while (!aux.IsAtEnd() && pdu.Decode(aux))
			{
				pdu.PrintOn(log);
				log << "\r\n";
			}

			log.close();
		}

		//Wait for reply
		waiting = false;

		//Reset counter
		counter = 0;

	} else{
		//Still waiting for repsonse
		if (counter++<200)
			//Wait for at least 20 to retransmit
			return NULL;

		//Reset counter
		counter = 0;

		//Retransmit
		cmd->Begin();

		//Logger::Debug("-Send retry CMD\n");
	}
	
	//Return cmd
	return cmd;
}

void H324CCSRLayer::OnPDUCompleted()
{
	//If it was response
	if (!isCmd)
	{
		//Delete the first element
		delete rpls.front();

		//Remove
		rpls.pop_front();
	} else {
		//Wait for response to delete
		waiting = true;
	}
}

int H324CCSRLayer::OnControlPDU(H324ControlPDU &pdu)
{
	//Exit
	return 1;
}

int H324CCSRLayer::IsSegmentable()
{
	//In fact it should be nonsegmentable and framed but.. 
	//for muxer its segmentable to send the closing flag
	return 1;
}

