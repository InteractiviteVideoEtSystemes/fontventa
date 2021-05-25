#ifndef _TEXT_H_
#define _TEXT_H_
#include <string>
#include "tools.h"
#include "media.h"

class UTF8Parser
{
public:
	UTF8Parser();
	UTF8Parser(const std::wstring& str);
	void Reset();
	DWORD Parse(const BYTE *data,DWORD size);
	bool IsParsed();
	void SetSize(DWORD size);
	DWORD GetUTF8Size();
	DWORD GetLength();
	std::wstring GetWString();
	const wchar_t* GetWChar();
	void SetWString(const std::wstring& str);
	void SetWChar(const wchar_t* buffer,DWORD bufferLen);
	DWORD SetString(const char* str);
	DWORD Serialize(BYTE *data,DWORD size);
	DWORD Serialize(std::string & str, bool append = false);
	DWORD Truncate(DWORD size);

private:
	std::wstring value;
	DWORD utf8size;
	DWORD bytes;
	DWORD len;
	wchar_t w;
};


class TextFrame : public MediaFrame
{
public:
	TextFrame() : MediaFrame(MediaFrame::Text,0)
	{
	}
	
	TextFrame(bool owns) : MediaFrame(MediaFrame::Text,0, owns)
	{
	}
	
	TextFrame(DWORD ts,const BYTE *buffer,DWORD bufferLen) : MediaFrame(MediaFrame::Text,bufferLen)
	{
		SetFrame(ts,buffer,bufferLen);
	}
	
	TextFrame(DWORD ts,const std::wstring& str) : MediaFrame(MediaFrame::Text,str.length()*4)
	{
		SetFrame(ts,str);
	}

	TextFrame(DWORD ts,const wchar_t* data,DWORD size) : MediaFrame(MediaFrame::Text,size*4)
	{
		SetFrame(ts,data,size);
	}

	void SetFrame(DWORD ts,const BYTE *buffer,DWORD bufferLen)
	{
		//Store value
		SetTimestamp(ts);
		//Reset parser
		parser.Reset();
		//Set lenght to be parsed
		parser.SetSize(bufferLen);
		//Parse
		parser.Parse(buffer,bufferLen);
		//Serialize to buffer
		if ( GetMaxMediaLength() < parser.GetUTF8Size() ) Alloc( parser.GetUTF8Size() );
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	void SetFrame(DWORD ts,const wchar_t *data,DWORD size)
	{
		//Store value
		SetTimestamp(ts);
		//Parse
		parser.SetWChar(data,size);

		if ( GetMaxMediaLength() < parser.GetUTF8Size() ) Alloc( parser.GetUTF8Size() );
		//Serialize to buffer
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	void SetFrame(DWORD ts,const std::wstring& str)
	{
		//Store value
		SetTimestamp(ts);
		//Parse
		parser.SetWString(str);
		if ( GetMaxMediaLength() < parser.GetUTF8Size() ) Alloc( parser.GetUTF8Size() );
		//Serialize to buffer
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	virtual MediaFrame* Clone()
	{
		//Create new one
		TextFrame *frame = new TextFrame(GetTimeStamp(),parser.GetWString());
		//Return it
		return (MediaFrame*)frame;
	}

	std::wstring GetWString()	{ return parser.GetWString();	}
	const wchar_t* GetWChar()	{ return parser.GetWChar();	}
	DWORD GetWLength()		{ return parser.GetLength();	}
private:
	UTF8Parser parser;
};

class TextInput
{
public:
	virtual TextFrame* GetFrame(DWORD timeout)=0;
	virtual void Cancel()=0;
};

class TextOutput
{
public:
	virtual int SendFrame(TextFrame &frame)=0;
};

#endif
