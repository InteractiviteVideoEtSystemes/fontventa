#ifndef _LOGO_H_
#define _LOGO_H_
#include "config.h"

class Logo
{
public:
	Logo();
	virtual ~Logo();
	Logo & operator =(const Logo& l);
	int Load(const char *filename, unsigned int width = 0, unsigned int height = 0);
	void Clean();
	
	BYTE* GetFrame();
	int GetWidth();
	int GetHeight();
	
	unsigned int GetSize() { return (((width/32+1)*32)*((height/32+1)*32)*3)/2; }

private:
	BYTE*	 frame;
	int width;
	int height;
};

#endif
