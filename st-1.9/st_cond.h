#pragma once
#include "st_vp.h"
class CStVp;
class CStCond {
public:
	CStCond();
	virtual ~CStCond();

	void st_cond_init(CStVp *vp);
	int st_cond_destroy();
	int st_cond_timedwait(st_utime_t timeout);
	int st_cond_wait();
	int _st_cond_signal(int broadcast);
	int st_cond_signal();
	
	_st_clist_t wait_q;	      /* Condition variable wait queue */
public:
	CStVp* m_vp;
};

