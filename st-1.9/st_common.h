#ifndef __ST_COMMON_H_NEW__
#define __ST_COMMON_H_NEW__


#define _ST_LAST_CLOCK_NEW(vp)             	((vp)->last_clock)

#define _ST_RUNQ_NEW(vp)           			((vp)->run_q)
#define _ST_IOQ_NEW(vp)                     ((vp)->io_q)
#define _ST_ZOMBIEQ_NEW(vp)                 ((vp)->zombie_q)
#ifdef DEBUG
#define _ST_THREADQ_NEW(vp)                 ((vp)->thread_q)
#endif

#define _ST_SLEEPQ_NEW(vp)                  ((vp)->sleep_q)
#define _ST_SLEEPQ_SIZE_NEW(vp)             ((vp)->sleepq_size)

#define _ST_VP_IDLE_NEW(vp)                	((vp)->m_io->m_event->st_epoll_dispatch)()


/*****************************************
 * vp queues operations
 */

#define _ST_ADD_IOQ_NEW(vp, _pq)    ST_APPEND_LINK(&_pq.links, &_ST_IOQ_NEW(vp))
#define _ST_DEL_IOQ_NEW(vp, _pq)    ST_REMOVE_LINK(&_pq.links)

#define _ST_ADD_RUNQ_NEW(vp, _thr)  ST_APPEND_LINK(&(_thr)->links, &_ST_RUNQ_NEW(vp))
#define _ST_DEL_RUNQ_NEW(vp, _thr)  ST_REMOVE_LINK(&(_thr)->links)

#define _ST_ADD_SLEEPQ_NEW(vp, _thr, _timeout)  ((vp)->st_add_sleep_q(_thr, _timeout))
#define _ST_DEL_SLEEPQ_NEW(vp, _thr)			((vp)->st_del_sleep_q(_thr))

#define _ST_ADD_ZOMBIEQ_NEW(vp, _thr)  ST_APPEND_LINK(&(_thr)->links, &_ST_ZOMBIEQ_NEW(vp))
#define _ST_DEL_ZOMBIEQ_NEW(vp, _thr)  ST_REMOVE_LINK(&(_thr)->links)

#ifdef DEBUG
#define _ST_ADD_THREADQ_NEW(vp, _thr)  ST_APPEND_LINK(&(_thr)->tlink, &_ST_THREADQ_NEW(vp))
#define _ST_DEL_THREADQ_NEW(vp, _thr)  ST_REMOVE_LINK(&(_thr)->tlink)
#endif

#define _ST_CURRENT_THREAD_NEW(vp)            ((vp)->m_curr_thread)
#define _ST_SET_CURRENT_THREAD_NEW(vp, _thread) ((vp)->m_curr_thread = (_thread))

//#ifdef DEBUG
//    void _st_iterate_threads(void);
//    #define ST_DEBUG_ITERATE_THREADS() _st_iterate_threads()
//#else
    #define ST_DEBUG_ITERATE_THREADS_NEW()
//#endif

#ifdef ST_SWITCH_CB
#define ST_SWITCH_OUT_CB_NEW(vp, _thread)		\
    if ((vp)->switch_out_cb != NULL &&	\
        _thread != (vp)->idle_thread &&	\
        _thread->state != _ST_ST_ZOMBIE) {	\
      	(vp)->switch_out_cb();		\
    }
#define ST_SWITCH_IN_CB_NEW(vp, _thread)		\
    if ((vp)->switch_in_cb != NULL &&	\
		_thread != (vp)->idle_thread &&	\
		_thread->state != _ST_ST_ZOMBIE) {	\
      	(vp)->switch_in_cb();		\
    }
#else
#define ST_SWITCH_OUT_CB_NEW(_thread)
#define ST_SWITCH_IN_CB_NEW(_thread)
#endif

/*
 * Switch away from the current thread context by saving its state and
 * calling the thread scheduler
 */
#define _ST_SWITCH_CONTEXT_NEW(vp, _thread)       \
    ST_BEGIN_MACRO                        \
    ST_SWITCH_OUT_CB_NEW(vp, _thread);            \
    if (!MD_SETJMP((_thread)->context)) { \
      (vp)->st_vp_schedule();                  \
    }                                     \
    ST_DEBUG_ITERATE_THREADS_NEW();           \
    ST_SWITCH_IN_CB_NEW(vp, _thread);             \
    ST_END_MACRO

/*
 * Restore a thread context that was saved by _ST_SWITCH_CONTEXT or
 * initialized by _ST_INIT_CONTEXT
 */
#define _ST_RESTORE_CONTEXT_NEW(vp, _thread)   \
    ST_BEGIN_MACRO                     \
    _ST_SET_CURRENT_THREAD_NEW(vp, _thread);   \
    MD_LONGJMP((_thread)->context, 1); \
    ST_END_MACRO

#define _ST_PAGE_SIZE_NEW(vp)                   ((vp)->pagesize)
#endif