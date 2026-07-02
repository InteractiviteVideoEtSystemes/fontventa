#ifndef TEXT2SUBTITLE_H_
#define	TEXT2SUBTITLE_H_

#include "text.h"
#include <list>
// Renommee TextEncoder -> Text2Subtitle pour lever la collision de nom avec la
// classe TextEncoder du mcu (encodeur temps reel threade), qui est une classe
// totalement differente. Ici il s'agit d'un accumulateur de sous-titres.
class Text2Subtitle
{
public:
	class Listener
	{
	public:
		virtual void onNewLine(std::string & prevline) = 0;
		virtual void onLineRemoved(std::string & prevline) = 0;
	};
public:
	Text2Subtitle();
	~Text2Subtitle();

	int Accumulate(const std::wstring & text);
	void GetCurrentLine(std::string & hist);
	void GetFirstHistoryLine(std::string & hist);
	void GetSubtitle(std::wstring & sub);
	void GetSubtitle(std::string & sub);
	TextFrame * GetSubTitle();
	void SetListener(Listener * l)
	{
		listener = l;
	}

	void GetFullText(std::string & text);

private:
	std::list<std::wstring> scroll;
	std::wstring line;
	TextFrame f;
	Listener * listener;

	void SendLineToHistory();
	void PopLineFromHistory();
};

class SubtitleToRtt
{
public:
	SubtitleToRtt() {};
	~SubtitleToRtt() {};

	void GetTextDiff(const std::wstring & sub, unsigned int & nbdel, std::wstring & diff);
	void GetTextDiff(const std::string & sub, unsigned int & nbdel, std::string & diff);
	void Reset();

private:
	std::wstring line;
	UTF8Parser parser;
};


#endif	/* TEXT2SUBTITLE_H_ */

