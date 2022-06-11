#pragma once

#include <Windows.h>


class CTimeTracker {
public:
	CTimeTracker(const WCHAR* name);

	~CTimeTracker() {
		EndTimeTrack();
	}

	void StartTimeTrack();
	static void MakeFileName();
	void EndTimeTrack();
	static void WriteTimeTrack();

	static void ResetTimeTrack();

private:
	int trackIdx;
};

#define PROFILE_MODE

#ifdef PROFILE_MODE
#define PROFILE_START(X) CTimeTracker TIMETRACK(X)
#define PROFILE_RESET() CTimeTracker::ResetTimeTrack()
#define PROFILE_WRITE() CTimeTracker::WriteTimeTrack()
#else
#define PROFILE_START(X)
#define PROFILE_RESET()
#define PROFILE_WRITE()
#endif 
