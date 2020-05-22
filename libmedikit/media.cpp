#include "medkit/log.h"
#include "medkit/media.h"


bool MediaFrame::PrependWithFrame(MediaFrame * f)
{
	if ( !ownsbuffer ) return false;	
	if ( f->GetType() != f->GetType() ) return false;

	DWORD oldSz = GetLength();
	DWORD newSz = f->GetLength() + oldSz;
	RtpPacketizationInfo oldRtpInfo = rtpInfo;
		
	if ( newSz > GetMaxMediaLength() )
	{
		Alloc(newSz);
	}
		
	// Move data of 
	memmove( buffer + f->GetLength(), buffer, oldSz );
		
	// prepend data
	memcpy( buffer, f->GetData(), f->GetLength() );
		
	// Adjust size
	SetLength(newSz);

	if ( HasRtpPacketizationInfo() )
	{
		// If frame has packetization, merge it
		// do not use ClearTypPacketizationInfo() as we reuse the ptr
		int i = 0;
		rtpInfo.clear();
			
		if ( f->HasRtpPacketizationInfo() )
		{
			for (RtpPacketizationInfo::iterator it = f->rtpInfo.begin(); it != f->rtpInfo.end(); it++)
			{
				MediaFrame::RtpPacketization * rtp = *it;
				AddRtpPacket(rtp->GetPos(), rtp->GetSize(), rtp->GetPrefixData(), rtp->GetPrefixLen(), rtp->IsMark());
				//Debug("RTP pkt %d: pos=%u, sz=%u. end=%u.\n", rtp->GetPos(), rtp->GetSize(), rtp->GetPos()+rtp->GetSize());
			}
		}
		else
		{
			// Add dummy packet
			AddRtpPacket(0, f->GetLength(), NULL, 0, false);
		}
		
		for (RtpPacketizationInfo::iterator it = oldRtpInfo.begin(); it != oldRtpInfo.end(); it++)
		{
			MediaFrame::RtpPacketization * rtp = (*it);
			rtpInfo.push_back(rtp);
		}
	}
	return true;
}
