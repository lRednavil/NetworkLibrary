#pragma once
#include <iostream>
#include <Windows.h>

extern int g_logLevel;
extern WCHAR g_logBuf[1024];
extern WCHAR g_fileLogBuf[16][1024];

extern DWORD g_fileLogIdx;
extern DWORD g_fileLogCnt;

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_SYSTEM 1
#define LOG_LEVEL_ERROR 2

void LogInit();

void FileLog(const WCHAR* fileName, int loglevel, const WCHAR* fmt, ...);
void FileLog(WCHAR* fileName, int loglevel, const WCHAR* fmt, ...);

inline void Log(WCHAR* str, int logLevel) {
    wprintf(L"%s \n", str);
}

#define _LOG(LogLevel, fmt, ...)    \
do{                                 \
    if(g_logLevel <= LogLevel){     \
        wsprintf(g_logBuf, fmt, ##__VA_ARGS__);  \
        Log(g_logBuf, LogLevel);                 \
    }                                            \
}while(0)                                    

#define _FILE_LOG(LogLevel, FileName, fmt, ...)    \
do{                                 \
    if(g_logLevel <= LogLevel){     \
        FileLog(FileName, LogLevel, fmt, ##__VA_ARGS__);                 \
    }                                            \
}while(0)                                    
