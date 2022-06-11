#include "pch.h"
#include <iostream>
#include <time.h>
#include <direct.h>
#include <thread>
#include "TimeTracker.h"

#define TRACK_MAX 100
#define THREAD_MAX 32
#define NAME_MAX 32

struct TIMETRACK {
	bool isAlive = false;
	WCHAR flagName[NAME_MAX];

	LARGE_INTEGER startTime;
	LARGE_INTEGER totalTime = { 0,0 };

	LARGE_INTEGER minTime;
	LARGE_INTEGER maxTime = { 0, 0 };

	int callCnt = 0;
};

struct THREADTRACK {
	DWORD threadID = NULL;
	TIMETRACK record[TRACK_MAX];
};

static WCHAR timeStr[28];
//작성할 시간 측정 폴더 디렉터리
static WCHAR fileDir[50] = L"TimeData\\";
//작성할 폴더 + 파일 이름
static WCHAR fileName[64];

LARGE_INTEGER freq;
THREADTRACK g_threadTrack[THREAD_MAX];

short g_threadIdx = 0;
DWORD threadTLS = TlsAlloc();

CTimeTracker::CTimeTracker(const WCHAR* name)
{
	int trackIdx;
	short threadIdx;
	TIMETRACK* track;

	if (TlsGetValue(threadTLS) == NULL) {
		TlsSetValue(threadTLS, (void*)InterlockedIncrement16(&g_threadIdx));
		g_threadTrack[(short)TlsGetValue(threadTLS) - 1].threadID = GetCurrentThreadId();
	}

	threadIdx = (short)TlsGetValue(threadTLS) - 1;
	for (trackIdx = 0; trackIdx < TRACK_MAX; trackIdx++) {
		track = &g_threadTrack[threadIdx].record[trackIdx];
		if (track->isAlive == false) {
			track->isAlive = true;
			track->callCnt = 0;
			track->totalTime.QuadPart = 0;
			track->minTime.QuadPart = MAXLONGLONG;
			track->maxTime.QuadPart = 0;
			wmemmove_s(track->flagName, NAME_MAX, name, NAME_MAX);
		}

		if (wcscmp(g_threadTrack[threadIdx].record[trackIdx].flagName, name) == 0) {
			break;
		}
	}

	if (trackIdx < TRACK_MAX) {
		this->trackIdx = trackIdx;
		StartTimeTrack();
	}

}

void CTimeTracker::StartTimeTrack() {
	short threadIdx = (short)TlsGetValue(threadTLS) - 1;

	QueryPerformanceCounter(&g_threadTrack[threadIdx].record[trackIdx].startTime);
}

void CTimeTracker::WriteTimeTrack() {
	TIMETRACK* track;

	//만약 데이터 폴더 미존재시
	_wmkdir(fileDir);

	_wsetlocale(LC_ALL, L"Korean");

	//frequency 얻어오기
	QueryPerformanceFrequency(&freq);

	FILE* fp;
	MakeFileName();

	_wfopen_s(&fp, fileName, L"wt");

	if (fp == NULL) {
		wprintf_s(L"File Write Failed. \n");
		return;
	}

	fwprintf_s(fp, L"------------------------------------------------------------------------------------------------------------- \n");
	fwprintf_s(fp, L"  Thread  |%32s|  Average Time  |    Min Time    |    Max Time    | Calls \n", L"Flag Name");

	for (short threadIdx = 0; threadIdx < THREAD_MAX; threadIdx++) {
		if (g_threadTrack[threadIdx].threadID == NULL){
			break;
		}
		
		fwprintf_s(fp, L"------------------------------------------------------------------------------------------------------------- \n");
		for (int trackIdx = 0; trackIdx < TRACK_MAX; trackIdx++) {
			track = &g_threadTrack[threadIdx].record[trackIdx];
			if (track->isAlive == false) break;

			LARGE_INTEGER average;
			average.QuadPart = (track->totalTime.QuadPart) - (track->maxTime.QuadPart) - (track->minTime.QuadPart);
			average.QuadPart *= 1000000;

			int averageDiv = max((track->callCnt - 2), 1);

			fwprintf_s(fp, L"%10u|%32s| %010lld µs | %010lld µs | %010lld µs | %d \n",
				g_threadTrack[threadIdx].threadID,
				track->flagName,
				average.QuadPart / freq.QuadPart / averageDiv,
				track->minTime.QuadPart * 1000000 / freq.QuadPart,
				track->maxTime.QuadPart * 1000000 / freq.QuadPart,
				track->callCnt);
		}
	}

	fclose(fp);
}

void CTimeTracker::ResetTimeTrack()
{
	int idx;

	for (short threadIdx = 0; threadIdx < THREAD_MAX; threadIdx++) {
		if (g_threadTrack[threadIdx].threadID == NULL) {
			break;
		}

		for (int trackIdx = 0; trackIdx < TRACK_MAX; trackIdx++) {
			g_threadTrack[threadIdx].record[trackIdx].isAlive =	false;
		}
	}
}


void CTimeTracker::EndTimeTrack() {
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);

	short threadIdx = (short)TlsGetValue(threadTLS) - 1;
	TIMETRACK* track;

	//저장용 예외처리
	if (threadIdx < 0) return;

	track = &g_threadTrack[threadIdx].record[trackIdx];

	LARGE_INTEGER deltaTime;
	deltaTime.QuadPart = endTime.QuadPart - track->startTime.QuadPart;

	track->totalTime.QuadPart += deltaTime.QuadPart;

	track->minTime.QuadPart = min(track->minTime.QuadPart, deltaTime.QuadPart);
	track->maxTime.QuadPart = max(track->maxTime.QuadPart, deltaTime.QuadPart);

	track->callCnt++;
}


//file함수에 사용할 fileName 작성 함수
void CTimeTracker::MakeFileName() {
	time_t _time;
	tm _tm;

	time(&_time);
	localtime_s(&_tm, &_time);

	swprintf_s(timeStr, L"TimeInfo%d%02d%02d_%02d%02d%02d", _tm.tm_year + 1900, _tm.tm_mon + 1, _tm.tm_mday, _tm.tm_hour, _tm.tm_min, _tm.tm_sec);

	wmemmove_s(fileName, 50, fileDir, 50);
	wcscat_s(fileName, timeStr);
	wcscat_s(fileName, L".txt");
}

#undef TRACK_MAX 
#undef THREAD_MAX