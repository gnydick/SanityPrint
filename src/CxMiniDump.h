#ifndef CxMiniDump_hpp_
#define CxMiniDump_hpp_
#pragma once
#include <Windows.h>
#include <tchar.h>
#include <DbgHelp.h>
//#pragma comment(lib, "dbghelp.lib")

#ifdef UNICODE
#define TSprintf	wsprintf
#else
#define TSprintf	sprintf
#endif
#include <string>
#include <sstream>
#include <iostream>
class MiniDump
{
private:
	MiniDump();
	~MiniDump();
	
public:
	// Whether to automatically generate a dump file when the program crashes;
	// Just call this function at the beginning of the main function;
	static void EnableAutoDump(bool bEnable = true);
	static std::string dumpDir();
private:

	static LONG ApplicationCrashHandler(EXCEPTION_POINTERS* pException);
	static void CreateDumpFile(LPCWSTR lpstrDumpFilePathName, EXCEPTION_POINTERS* pException);
};

#endif	