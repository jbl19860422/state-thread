#ifndef _ST_VP_H_
#define _ST_VP_H_

#include "st_io.h"
#include "st_time.h"
#include "st_common.h"
#include <sys/mman.h>


#define REDZONE_NEW(vp)	_ST_PAGE_SIZE_NEW(vp)


class CStIO;
class CStVp {
public:
	CStVp();
	_st_thread_t *m_curr_thread;
	int m_st_active_count;
	
	int 	st_init(void);
	int 	st_sleep(int secs);
	int 	st_usleep(st_utime_t usecs);
	int 	st_poll(struct pollfd *pds, int npds, st_utime_t timeout);
	void 	st_add_sleep_q(_st_thread_t *trd, st_utime_t timeout);
	void 	st_del_sleep_q(_st_thread_t *trd);
	void 	st_vp_schedule(void);
	_st_thread_t **heap_insert_new(_st_thread_t *thread);
	void 	heap_delete_new(_st_thread_t *thread);
	int getEpollDataSize();
	
	_st_thread_t* st_thread_create(void *(*start)(void *arg), void *arg, int joinable, int stk_size);
	void 	st_thread_main(void);
	static void* st_idle_thread_start(void* arg);
	void* _st_idle_thread_start();
	void 	st_thread_exit(void *retval);
	void st_thread_interrupt(_st_thread_t* trd);
	int st_thread_join(_st_thread_t *trd, void **retvalp);
	void 	st_vp_check_clock(void);

	/**********cond**************/
	_st_cond_t *st_cond_new(void);
	int st_cond_destroy(_st_cond_t *cvar);
	int st_cond_timedwait(_st_cond_t *cvar, st_utime_t timeout);
	int st_cond_wait(_st_cond_t *cvar);
	int _st_cond_signal(_st_cond_t *cvar, int broadcast);
	int st_cond_signal(_st_cond_t *cvar);
	int st_cond_broadcast(_st_cond_t *cvar);
	
public:
	_st_thread_t 	*idle_thread;  /* Idle thread for this vp */
	st_utime_t 		last_clock;      /* The last time we went into vp_check_clock() */

	_st_clist_t 	run_q;          /* run queue for this vp */
	_st_clist_t 	io_q;           /* io queue for this vp */
	_st_clist_t 	zombie_q;       /* zombie queue for this vp */
#ifdef DEBUG
	_st_clist_t 	thread_q;       /* all threads of this vp */
#endif
	int pagesize;

	_st_thread_t 	*sleep_q;      /* sleep queue for this vp */
	int 			sleepq_size;	      /* number of threads on sleep queue */

#ifdef ST_SWITCH_CB
	st_switch_cb_t 	switch_out_cb;	/* called when a thread is switched out */
	st_switch_cb_t 	switch_in_cb;	/* called when a thread is switched in */
#endif
	CStIO			*m_io;
	CStTime			m_st_time;

	int				m_st_num_free_stacks;
	int				m_st_randomize_stacks;
	_st_clist_t 	m_st_free_stacks;
	_st_stack_t*	st_stack_new(int stack_size);
	void 			st_stack_free(_st_stack_t *ts);
	char*			st_new_stk_segment(int size);
	time_t 			m_st_curr_time;       /* Current time as returned by time(2) */
	st_utime_t 		m_st_last_tset;       /* Last time it was fetched */
};

#endif
