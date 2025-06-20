#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <set>
#include <list>
#include <medkit/textencoder.h>
#include <medkit/log.h>
#include <medkit/tools.h>
#include <medkit/text.h>


/**********************************
* TextEncoder
*	Constructor
***********************************/
TextEncoder::TextEncoder()
{
	listener = NULL;
}

/*******************************
* ~TextEncoder
*	Destructor.
********************************/
TextEncoder::~TextEncoder()
{
}

void TextEncoder::SendLineToHistory()
{
	//push the line
	scroll.push_back(line);
	if (listener)
	{
		std::string utf8line;
    		UTF8Parser p(line);
    		p.Serialize(utf8line);
		listener->onNewLine(utf8line);
		//Log("Pushed line [%s].\n", utf8line.c_str());
	}
	//Empty it
	line.clear();
}

void TextEncoder::PopLineFromHistory()
{
	if (scroll.size() > 0)
	{
		line = scroll.back();
		scroll.pop_back();
	        if (listener)
        	{
                	std::string utf8line;
                	UTF8Parser p(line);
                	p.Serialize(utf8line);
                	listener->onLineRemoved(utf8line);
		}
        }
}
/*******************************************
* Encode
*	Capturamos el text y lo mandamos
*******************************************/
int TextEncoder::Accumulate(const std::wstring & text)
{
	//If it has content
	if (! text.empty())
	{
		int ret = 1;
		int nb_backspaces = 0;
		//Search each character
		for (int i=0;i<text.length();i++)
		{
			//Get char
			wchar_t ch = text.at(i);

			//Depending on the char
			switch (ch)
			{
				//0xA - ignore if if previous char is 0x0D
				case 0x0A:
					if (i > 0 && text.at(i-1) == 0x0D)
					{
					}
					else
					{
						//Append an end line
						line.push_back(0x0A);
						//push the line
						SendLineToHistory();
						ret = 2;
					}
					break;

				case 0x2028:
				case 0x2029:
				case 0x0D:
					//Append an end line
					line.push_back(0x0A);
					//push the line
					SendLineToHistory();
					ret = 2;
					break;
				//Check backspace
				case 0x08:
					//If not empty
					if (line.size() == 0)
					{
						PopLineFromHistory();
						Log("RTT: editing previous line\n");
					}
					//Remove last
					if (line.size())
					{
						line.erase(line.length()-1,1);
						nb_backspaces++;
					}
					break;
				//BOM
				case 0xFEFF:
					//Do nothing
					break;
				//Replacement
				case 0xFFFD:
					//Append .
					line.push_back('.');
					break;
#if 0 /* disable line wrap */
				//Any other
				case ' ':
					line.push_back(ch);
					if (line.size() > 80 )
					{
						line.push_back(0x0A);
						//push the line
						SendLineToHistory();
						ret = 2;
					}
					break;

				case '-':
					line.push_back(ch);
					if (line.size() > 80 )
					{
						line.push_back(0x0A);
						//push the line
						SendLineToHistory();
						ret = 2;
					}
					break;

				//Any other
#endif
				default:
					//Append it
					line.push_back(ch);
					break;
			}
			if (ch != 0x08)
			{
				nb_backspaces = 0;
			}
		}
		return ret;
	}
	return 0;
}

void TextEncoder::GetSubtitle(std::wstring & sub)
{
	//Append lines in scroll
	for (std::list<std::wstring>::iterator it=scroll.begin();it!=scroll.end();it++)
		//Append
		sub += (*it);

	//Append line also
	sub += line;

	//Check number of lines in scroll
	while (scroll.size()>2) scroll.pop_front();

//	Log("Subititle has %d lines. Content: %ls.\n",
//	    scroll.size(), sub.c_str() );
}

void TextEncoder::GetSubtitle(std::string & sub)
{
    std::wstring subw;
    GetSubtitle(subw);
    UTF8Parser p(subw);
    p.Serialize(sub);
}

void TextEncoder::GetFirstHistoryLine(std::string & hist)
{
    if ( scroll.size() > 0 )
    {
	std::wstring & subw = scroll.back();
	UTF8Parser p(subw);
	Log("First line: %ls.\n", subw.c_str() );

	p.Serialize(hist);
    }
    else
    {
	Log("No history.\n" );
	hist.clear();
    }
}

void TextEncoder::GetCurrentLine(std::string & curline)
{
    if ( line.size() > 0 )
    {
		UTF8Parser p(line);
		p.Serialize(curline);
    }
    else
    {
		curline.clear();
    }
}

void TextEncoder::GetFullText(std::string & text)
{
	//Append lines in scroll
	text.clear();
	for (std::list<std::wstring>::iterator it=scroll.begin(); it!=scroll.end(); it++)
	{
		UTF8Parser p(*it);
		//Append
		p.Serialize(text, true);
	}

	if (line.size() > 0)
	{
		UTF8Parser p(line);
		p.Serialize(text, true);
		text.push_back('\n');
	}
}

void SubtitleToRtt::GetTextDiff(const std::string & sub, unsigned int & nbdel, std::string & diff)
{
	std::wstring wdiff;	
	wdiff.clear();
	parser.Reset();
	parser.Parse( (BYTE *) sub.data(), sub.length() );
	GetTextDiff(parser.GetWString(), nbdel, wdiff);
	if (!wdiff.empty() )
	{
		parser.SetWString(wdiff);
		parser.Serialize(diff);
	}
}

void SubtitleToRtt::GetTextDiff(const std::wstring & sub, unsigned int & nbdel, std::wstring & diff)
{
	unsigned int len = line.length();
	unsigned int len2 = sub.length();
	unsigned int diffidx;

	for (diffidx = 0; diffidx<len; diffidx++)
	{
		if (diffidx >= len2) break;
		
		if (sub[diffidx] != line[diffidx] ) break;
	}
	
	nbdel = 0;
	
	if (diffidx < len2 && diffidx >= len)
	{
		diff = sub.substr(diffidx);
		if (diffidx == 0)
		{
			diff.insert(0, 1, '\r');
			diff.insert(0, 1, '\n');
		}
	}
	else if (diffidx > len2 && diffidx<len)
	{
		diff.clear();
		nbdel = diffidx - len2;
	}
	else
	{
		// TODO: manage insersion and deletion here
		diff.clear();
	}
	line = sub;
}

void SubtitleToRtt::Reset()
{
	line.clear();
}
