#pragma once
class CLock
{
public:
	//0 for shared, 1 for exclusive
	CLock(SRWLOCK* srw, char lockType);
	~CLock();


private:
	SRWLOCK* lock;
	char type;
};

