/* 
 * File:   h264depacketizer.h
 * Author: Sergio
 *
 * Created on 26 de enero de 2012, 9:46
 */

#ifndef H264DEPACKETIZER_H
#define	H264DEPACKETIZER_H

#include "../medkit/video.h"

class H264Depacketizer
{
public:
	H264Depacketizer();
	virtual ~H264Depacketizer();
	virtual void SetTimestamp(DWORD timestamp);
	virtual MediaFrame* AddPayload(BYTE* payload,DWORD payload_len, bool mark);
	virtual void ResetFrame();

	void SetUseStartCode(bool use)
	{
		useStartCode = use;
	}
	
	bool MayBeIntra() { return hasPPS || hasSPS || frame.IsIntra(); }
private:
	VideoFrame frame;
	DWORD iniFragNALU;
	bool useStartCode;
	bool hasPPS;
	bool hasSPS;
};

#endif	/* H264DEPACKETIZER_H */

