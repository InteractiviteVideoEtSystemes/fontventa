#ifndef TEXTENCODER_H_
#define	TEXTENCODER_H_

#include "text.h"
#include <list>
class TextEncoder
{
public:
	TextEncoder();
	~TextEncoder();

	int Accumulate(const std::wstring & text);
	void GetCurrentLine(std::string & hist);
	void GetFirstHistoryLine(std::string & hist);
	void GetSubtitle(std::wstring & sub);
	void GetSubtitle(std::string & sub);
	TextFrame * GetSubTitle();
private:
	std::list<std::wstring> scroll;
	std::wstring line;
	TextFrame f;
};

class SubtitleToRtt
{
public:
	SubtitleToRtt() {};
	~SubtitleToRtt() {};
	
	void GetTextDiff(const std::wstring & sub, unsigned int & nbdel, std::wstring & diff);
	void GetTextDiff(const std::string & sub, unsigned int & nbdel, std::string & diff);

private:
	std::wstring line;
	UTF8Parser parser;
};


#endif	/* TEXTENCODER_H */

