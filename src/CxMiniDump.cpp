#include "libslic3r/Utils.hpp"
#include "CxMiniDump.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <windows.h>
#include <shlobj_core.h>
#include <iostream>
#include <comdef.h>
#include "libslic3r/GlobalConfig.hpp"
#include "libslic3r/AutomationMgr.hpp"

std::string PWSTRToString(PWSTR pwsz) {
	// Convert PWSTR to std::wstring
	std::wstring wstr(pwsz);

	// Convert std::wstring to std::string
	std::string str(wstr.begin(), wstr.end());

	return str;
}

static std::string get_cpu_model()
{
	constexpr DWORD bufsize_ = 500;
	DWORD           bufsize  = bufsize_ - 1;
	char            buf[bufsize_] = "";
	memset(buf, 0, sizeof(buf));
	const std::string reg_path = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
	if (RegGetValueA(HKEY_LOCAL_MACHINE, reg_path.c_str(), "ProcessorNameString",
		RRF_RT_REG_SZ, NULL, &buf, &bufsize) == ERROR_SUCCESS) {
		return std::string(buf);
	}
	return std::string();
}

std::wstring GetExecutableDirectory() {
	wchar_t path_to_exe[MAX_PATH + 1] = { 0 };
	// Get the full path of the current executable
	if (GetModuleFileNameW(NULL, path_to_exe, MAX_PATH) == 0) {
		// Handle error
		return L"";
	}
	// Convert the path to std::wstring
	std::wstring fullPath(path_to_exe);

	// Find the position of the last backslash
	size_t pos = fullPath.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		// Extract the directory part
		return fullPath.substr(0, pos);
	}

	return L"";
}
bool StartAnotherExe(const std::wstring& exePath, const std::wstring& params) {
	// Add quotes at the beginning and end of the parameter string
	std::wstring quotedParams = L"\"" + params + L"\"";
	HINSTANCE result = ShellExecute(NULL, L"open", exePath.c_str(), params.c_str(), NULL, SW_SHOWNORMAL);
	if ((int)result <= 32) {
		std::wcerr << L"Failed to start process: " << exePath << std::endl;
		return false;
	}
	return true;
}
std::string GetLanguageName()
{
	// Get the current user's default language ID
	LANGID langID = GetUserDefaultLangID();

	// Get the length of the language name
	int length = GetLocaleInfoA(MAKELCID(langID, SORT_DEFAULT), LOCALE_SISO639LANGNAME, NULL, 0);
	if (length == 0)
	{
		return "";
	}

	// Get the language name
	std::string languageName(length, '\0');
	GetLocaleInfoA(MAKELCID(langID, SORT_DEFAULT), LOCALE_SISO639LANGNAME, &languageName[0], length);

	return languageName;
}

MiniDump::MiniDump()
{
}

MiniDump::~MiniDump()
{
}
std::string MiniDump::dumpDir()
{
	PWSTR path = NULL;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path))) {
		std::wcout << L"AppData Path: " << path << std::endl;
		CoTaskMemFree(path);
	}
	else {
		std::wcout << L"Failed to get the AppData path" << std::endl;
	}
	//A.0
	std::string versionDir = SANITYPRINT_VERSION_MAJOR + std::string(".0");
	std::string version = std::string(PROJECT_VERSION_EXTRA);
	bool        is_alpha = boost::algorithm::icontains(version, "alpha");
	if (is_alpha) {
		versionDir = versionDir + std::string(" Alpha");
	}
	//save to log folder
	const std::string dumpDir = PWSTRToString(path) + "\\" + SLIC3R_APP_FOLDER_KEY + "\\" + SLIC3R_APP_USE_FORDER + "\\" + versionDir + "\\log";
	if (!dumpDir.empty() && !boost::filesystem::exists(dumpDir)) {
		boost::filesystem::create_directories(dumpDir);
	}
	return dumpDir;
}
void MiniDump::EnableAutoDump(bool bEnable)
{
	if (bEnable)
	{
		//std::string xxx = dumpDir();
		SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER) ApplicationCrashHandler);
	}
}

LONG MiniDump::ApplicationCrashHandler(EXCEPTION_POINTERS *pException)
{
	/*if (IsDebuggerPresent())
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}*/

	TCHAR szDumpDir[MAX_PATH] = { 0 };
	TCHAR szDumpFile[MAX_PATH] = { 0 };
	TCHAR szMsg[MAX_PATH] = { 0 };
	SYSTEMTIME	stTime = { 0 };
	// Build the dump file path;
	GetLocalTime(&stTime);
	std::string dumpStr = dumpDir();
	std::wstring strDumpPath(dumpStr.begin(), dumpStr.end());

	std::string processNameStr = SLIC3R_PROCESS_NAME + std::string("_") + SANITYPRINT_VERSION + std::string("_") + PROJECT_VERSION_EXTRA;
	// Convert std::string to std::wstring
	std::wstring processNameWStr(processNameStr.begin(), processNameStr.end());
	::GetCurrentDirectory(MAX_PATH, szDumpDir);
	TSprintf(szDumpFile, _T("%s\\%04d%02d%02d_%02d%02d%02d_%s.dmp"), strDumpPath.c_str(),
		stTime.wYear, stTime.wMonth, stTime.wDay,
		stTime.wHour, stTime.wMinute, stTime.wSecond,
		processNameWStr.c_str());
#if AUTOMATION_TOOL
    if (Slic3r::AutomationMgr::enabled()) 
	{
        Slic3r::AutomationMgr::outputLog("Dump Error", 1);
	}
#endif // AUTOMATION_TOOL

	// Create the dump file;
	CreateDumpFile(szDumpFile, pException);
	
	std::wstring exePath = GetExecutableDirectory() + L"/resources/dumptools/dumptool.exe";
	//"C:/Users/Administrator/AppData/Roaming/Creality/New C3D/dump/20241023_114858_SanityPrint_6.0.0.382_Alpha.dmp" "" "6.0.0.192" "zh_CN"
	std::string version = SANITYPRINT_VERSION;
	std::wstring wVersion(version.begin(), version.end());
	std::string lang = GlobalConfig::getInstance()->getCurrentLanguage();
	if (lang.empty()) {
		lang = GetLanguageName();
	}
	if (lang != "zh_CN" && lang != "zh_TW") {
		lang = "en";
	}
	std::wstring ws_lang(lang.begin(), lang.end());
	//std::wstring params = L"\"" + std::wstring(szDumpFile) + L"\" \"\" \"" + wVersion + L"\"";
	std::wstring params = L"\"" + std::wstring(szDumpFile) + L"\" \"\" \"" + wVersion + L"\" \"" + ws_lang + L"\"";
	// Replace backslashes with forward slashes
	std::wstring modifiedParams = std::wstring(params.begin(), params.end());
	std::replace(modifiedParams.begin(), modifiedParams.end(), L'\\', L'/');

	// Start another process;
	bool runSuccess = StartAnotherExe(exePath, modifiedParams);
	if(!runSuccess){
		// Pop up an error dialog or prompt for upload, then exit the program;
		TSprintf(szMsg, _T("I'm so sorry, but the program crashed.\r\ndump file : %s"), szDumpFile);
		FatalAppExit(-1, szMsg);
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

void MiniDump::CreateDumpFile(LPCWSTR strPath, EXCEPTION_POINTERS *pException)
{
	// Create the dump file;
	HANDLE hDumpFile = CreateFile(strPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	// Dump information;
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;

	// Attach a user stream containing the full CPU model info, so the model can be recovered from the dump alone later
	std::string cpu_model = get_cpu_model();
	std::string comment;
	if (!cpu_model.empty()) {
		comment = "CPU: " + cpu_model;
	} else {
		comment = "CPU: <unknown>";
	}

	MINIDUMP_USER_STREAM cpu_stream;
	cpu_stream.Type = CommentStreamA;
	cpu_stream.BufferSize = static_cast<ULONG>(comment.size() + 1);
	cpu_stream.Buffer = const_cast<char*>(comment.c_str());

	MINIDUMP_USER_STREAM_INFORMATION user_streams;
	user_streams.UserStreamCount = 1;
	user_streams.UserStreamArray = &cpu_stream;

	// Write the dump file contents;
	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, &user_streams, NULL);
	CloseHandle(hDumpFile);
}
