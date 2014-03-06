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
}

/*******************************
* ~TextEncoder
*	Destructor.
********************************/
TextEncoder::~TextEncoder()
{
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
		//Search each character
		for (int i=0;i<text.length();i++)
		{
			//Get char
			wchar_t ch = text.at(i);
			//Depending on the char
			switch (ch)
			{
				//MEssage delimiter
				case 0x0A:
				case 0x2028:
				case 0x2029:
					//Append an end line
					line.push_back(0x0A);
					//push the line
					scroll.push_back(line);
					//Empty it
					line.clear();
					break;
				//Check backspace
				case 0x08:
					//If not empty
					if (line.size())
						//Remove last
						line.erase(line.length()-1,1);
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
				//Any other
				case ' ':
					if (line.size() > 40 )
					{
						line.push_back(0x0A);
						//push the line
						scroll.push_back(line);
						//Empty it
						line.clear();
					}
					else
					{
						line.push_back(ch);
					}
					break;

				case '-':
					line.push_back(ch);
					if (line.size() > 40 )
					{
						line.push_back(0x0A);
						//push the line
						scroll.push_back(line);
						//Empty it
						line.clear();
					}
					break;

				default:
					//Append it
					line.push_back(ch);
			}
		}
	}
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
}

void TextEncoder::GetSubtitle(std::string & sub)
{
    std::wstring subw;
    GetSubtitle(subw);
    UTF8Parser p(subw);
    p.Serialize(sub);
}