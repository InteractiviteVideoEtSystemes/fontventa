#ifndef TEXTENCODER_H_
#define	TEXTENCODER_H_
#include "text.h"

class TextEncoder
{
public:
	TextEncoder();
	~TextEncoder();

	int Accumulate(const std::wstring & text);
	void GetSubtitle(std::wstring & sub);
	TextFrame * GetSubTitle();
private:
	std::list<std::wstring> scroll;
	std::wstring line;
	TextFrame f;
};

#endif	/* TEXTENCODER_H */

