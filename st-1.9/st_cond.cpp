#include "st_cond.h"

CStCond::CStCond()
{
	
}

void CStCond::st_cond_init(CStVp *vp)
{
	m_vp = vp;
	ST_INIT_CLIST(&wait_q);
}

int CStCond::st_cond_destroy()
{
	if (wait_q.next != &wait_q) {
        errno = EBUSY;
        return -1;
    }
}

int CStCond::st_cond_timedwait(st_utime_t timeout)
{
    _st_thread_t *me = _ST_CURRENT_THREAD_NEW(m_vp);
    int rv;
    
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        return -1;
    }
    
    /* Put caller thread on the condition variable's wait queue */
    me->state = _ST_ST_COND_WAIT;
    ST_APPEND_LINK(&me->wait_links, &wait_q);
    
    if (timeout != ST_UTIME_NO_TIMEOUT) {
        _ST_ADD_SLEEPQ_NEW(m_vp, me, timeout);
    }
    
    _ST_SWITCH_CONTEXT_NEW(m_vp, me);
    
    ST_REMOVE_LINK(&me->wait_links);
    rv = 0;
    
    if (me->flags & _ST_FL_TIMEDOUT) {
        me->flags &= ~_ST_FL_TIMEDOUT;
        errno = ETIME;
        rv = -1;
    }
    if (me->flags & _ST_FL_INTERRUPT) {
        me->flags &= ~_ST_FL_INTERRUPT;
        errno = EINTR;
        rv = -1;
    }
    
    return rv;
}

int CStCond::st_cond_wait()
{
    return st_cond_timedwait(ST_UTIME_NO_TIMEOUT);
}

int CStCond::_st_cond_signal(int broadcast)
{
    _st_thread_t *thread;
    _st_clist_t *q;
    
    for (q = wait_q.next; q != &wait_q; q = q->next) {
        thread = _ST_THREAD_WAITQ_PTR(q);
        if (thread->state == _ST_ST_COND_WAIT) {
            if (thread->flags & _ST_FL_ON_SLEEPQ) {
                _ST_DEL_SLEEPQ_NEW(m_vp, thread);
            }
            
            /* Make thread runnable */
            thread->state = _ST_ST_RUNNABLE;
            _ST_ADD_RUNQ_NEW(m_vp, thread);
            if (!broadcast) {
                break;
            }
        }
    }
    
    return 0;
}

int CStCond::st_cond_signal()
{
    return _st_cond_signal(0);
}

int CStCond::st_cond_broadcast()
{
    return _st_cond_signal(1);
}

CStCond::~CStCond()
{
   	
}



