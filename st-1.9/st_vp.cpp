#include "common.h"
#include "st_vp.h"

#include "stdio.h"
CStVp::CStVp()
{
	m_st_active_count = 0;
	m_st_num_free_stacks = 0;
	m_st_randomize_stacks = 0;
	m_st_curr_time = 0;
	m_st_free_stacks = ST_INIT_STATIC_CLIST(&m_st_free_stacks);
	idle_thread = NULL;
}

int CStVp::getEpollDataSize()
{
	return m_io->m_event->m_st_epoll_data->fd_data_size;
}

int CStVp::st_init(void)
{
	_st_thread_t *thread;

	if (m_st_active_count) {
		/* Already initialized */
		return 0;
	}

	m_io = new CStIO;
	m_io->m_vp = this;
	if (m_io->st_io_init() < 0)
		return -1;
//	printf("m_io.data_size=%d",m_io->m_event->m_st_epoll_data->fd_data_size);
	ST_INIT_CLIST(&_ST_RUNQ_NEW(this));
	ST_INIT_CLIST(&_ST_IOQ_NEW(this));
	ST_INIT_CLIST(&_ST_ZOMBIEQ_NEW(this));
#ifdef DEBUG
	ST_INIT_CLIST(&_ST_THREADQ_NEW(this));
#endif

	pagesize = getpagesize();
//	printf("*****************pagesize=%d",pagesize);
	last_clock = m_st_time.st_utime();

	/*
	* Create idle thread
	*/
	idle_thread = st_thread_create(st_idle_thread_start, this, 0, 0);
	if (!idle_thread)
	{
//		printf("\nidle_thread create failed.\n");
		return -1;
	}
//	printf("idle_thread create success**************.\n");
//	printf("m_io.data_size1=%d\n",m_io->m_event->m_st_epoll_data->fd_data_size);
	idle_thread->flags = _ST_FL_IDLE_THREAD;
	m_st_active_count--;
	_ST_DEL_RUNQ_NEW(this, idle_thread);

	/*
	* Initialize primordial thread
	*/
	thread = (_st_thread_t *) calloc(1, sizeof(_st_thread_t) + (ST_KEYS_MAX * sizeof(void *)));
	if (!thread)
		return -1;
	thread->private_data = (void **) (thread + 1);
	thread->state = _ST_ST_RUNNING;
	thread->flags = _ST_FL_PRIMORDIAL;
//	printf("m_io.data_size2=%d\n",m_io->m_event->m_st_epoll_data->fd_data_size);
	_ST_SET_CURRENT_THREAD_NEW(this, thread);
	m_st_active_count++;
//	printf("m_io.data_size3=%d\n",m_io->m_event->m_st_epoll_data->fd_data_size);
#ifdef DEBUG
	_ST_ADD_THREADQ_NEW(this, thread);
#endif
//	printf("m_io.data_size4=%d\n",m_io->m_event->m_st_epoll_data->fd_data_size);
	return 0;
}

void* CStVp::st_idle_thread_start(void *arg)
{
  CStVp* vp = (CStVp*)arg;
  vp->_st_idle_thread_start();
  return NULL;
}

void* CStVp::_st_idle_thread_start()
{
  _st_thread_t *me = _ST_CURRENT_THREAD_NEW(this);

  while (m_st_active_count > 0) {
    /* Idle vp till I/O is ready or the smallest timeout expired */
//  	printf("m_st_active_count=%d\n",m_st_active_count);
//  	printf("m_io.data_size1=%d\n",m_io->m_event->m_st_epoll_data->fd_data_size);
    _ST_VP_IDLE_NEW(this);

    /* Check sleep queue for expired threads */
    st_vp_check_clock();

    me->state = _ST_ST_RUNNABLE;
    _ST_SWITCH_CONTEXT_NEW(this, me);
  }

  /* No more threads */
  exit(0);

  /* NOTREACHED */
  return NULL;
}

int CStVp::st_thread_join(_st_thread_t *trd, void **retvalp)
{
    _st_cond_t *term = trd->term;
    
    /* Can't join a non-joinable thread */
    if (term == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (_ST_CURRENT_THREAD_NEW(this) == trd) {
        errno = EDEADLK;
        return -1;
    }
    
    /* Multiple threads can't wait on the same joinable thread */
    if (term->wait_q.next != &term->wait_q) {
        errno = EINVAL;
        return -1;
    }
    
    while (trd->state != _ST_ST_ZOMBIE) {
        if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0) {
            return -1;
        }
    }
    
    if (retvalp) {
        *retvalp = trd->retval;
    }
    
    /*
    * Remove target thread from the zombie queue and make it runnable.
    * When it gets scheduled later, it will do the clean up.
    */
    trd->state = _ST_ST_RUNNABLE;
    _ST_DEL_ZOMBIEQ_NEW(this, trd);
    _ST_ADD_RUNQ_NEW(this, trd);
    
    return 0;
}



void CStVp::st_vp_check_clock(void)
{
  _st_thread_t *thread;
  st_utime_t elapsed, now;
 
  now = m_st_time.st_utime();
  elapsed = now - _ST_LAST_CLOCK_NEW(this);
  _ST_LAST_CLOCK_NEW(this) = now;

  if (m_st_curr_time && now - m_st_last_tset > 999000) {
    m_st_curr_time = time(NULL);
    m_st_last_tset = now;
  }

  while (_ST_SLEEPQ_NEW(this) != NULL) {
    thread = _ST_SLEEPQ_NEW(this);
    ST_ASSERT(thread->flags & _ST_FL_ON_SLEEPQ);
    if (thread->due > now)
      break;
    _ST_DEL_SLEEPQ_NEW(this, thread);

    /* If thread is waiting on condition variable, set the time out flag */
    if (thread->state == _ST_ST_COND_WAIT)
      thread->flags |= _ST_FL_TIMEDOUT;

    /* Make thread runnable */
    ST_ASSERT(!(thread->flags & _ST_FL_IDLE_THREAD));
    thread->state = _ST_ST_RUNNABLE;
    _ST_ADD_RUNQ_NEW(this, thread);
  }
}

int CStVp::st_sleep(int secs)
{
  return st_usleep((secs >= 0) ? secs * (st_utime_t) 1000000LL :
				 ST_UTIME_NO_TIMEOUT);
}

int CStVp::st_usleep(st_utime_t usecs)
{
  _st_thread_t *me = _ST_CURRENT_THREAD_NEW(this);

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  if (usecs != ST_UTIME_NO_TIMEOUT) {
    me->state = _ST_ST_SLEEPING;
    _ST_ADD_SLEEPQ_NEW(this, me, usecs);
  } else
    me->state = _ST_ST_SUSPENDED;

  _ST_SWITCH_CONTEXT_NEW(this, me);

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  return 0;
}


_st_thread_t *CStVp::st_thread_create(void *(*start)(void *arg), void *arg, int joinable, int stk_size)
{
  _st_thread_t *thread;
  _st_stack_t *stack;
  void **ptds;
  char *sp;
#ifdef __ia64__
  char *bsp;
#endif

  /* Adjust stack size */
  if (stk_size == 0)
    stk_size = ST_DEFAULT_STACK_SIZE;
  stk_size = ((stk_size + _ST_PAGE_SIZE_NEW(this) - 1) / _ST_PAGE_SIZE_NEW(this)) * _ST_PAGE_SIZE_NEW(this);
  stack = st_stack_new(stk_size);
  if (!stack)
  {
  	printf("create stack failed 1\n");
  	return NULL;
  }

  /* Allocate thread object and per-thread data off the stack */
#if defined (MD_STACK_GROWS_DOWN)
  sp = stack->stk_top;
#ifdef __ia64__
  /*
   * The stack segment is split in the middle. The upper half is used
   * as backing store for the register stack which grows upward.
   * The lower half is used for the traditional memory stack which
   * grows downward. Both stacks start in the middle and grow outward
   * from each other.
   */
  sp -= (stk_size >> 1);
  bsp = sp;
  /* Make register stack 64-byte aligned */
  if ((unsigned long)bsp & 0x3f)
    bsp = bsp + (0x40 - ((unsigned long)bsp & 0x3f));
  stack->bsp = bsp + _ST_STACK_PAD_SIZE;
#endif
  sp = sp - (ST_KEYS_MAX * sizeof(void *));
  ptds = (void **) sp;
  sp = sp - sizeof(_st_thread_t);
  thread = (_st_thread_t *) sp;

  /* Make stack 64-byte aligned */
  if ((unsigned long)sp & 0x3f)
    sp = sp - ((unsigned long)sp & 0x3f);
  stack->sp = sp - _ST_STACK_PAD_SIZE;
#elif defined (MD_STACK_GROWS_UP)
  sp = stack->stk_bottom;
  thread = (_st_thread_t *) sp;
  sp = sp + sizeof(_st_thread_t);
  ptds = (void **) sp;
  sp = sp + (ST_KEYS_MAX * sizeof(void *));

  /* Make stack 64-byte aligned */
  if ((unsigned long)sp & 0x3f)
    sp = sp + (0x40 - ((unsigned long)sp & 0x3f));
  stack->sp = sp + _ST_STACK_PAD_SIZE;
#else
#error Unknown OS
#endif

  memset(thread, 0, sizeof(_st_thread_t));
  memset(ptds, 0, ST_KEYS_MAX * sizeof(void *));

  /* Initialize thread */
  thread->private_data = ptds;
  thread->stack = stack;
  thread->start = start;
  thread->arg = arg;

#ifndef __ia64__
  _ST_INIT_CONTEXT(thread, stack->sp, st_thread_main);
#else
  _ST_INIT_CONTEXT(thread, stack->sp, stack->bsp, st_thread_main);
#endif

  /* If thread is joinable, allocate a termination condition variable */
  if (joinable) {
    thread->term = st_cond_new();
    if (thread->term == NULL) {
      st_stack_free(thread->stack);
	  printf("st_cond_new failed 1\n");
      return NULL;
    }
  }

  /* Make thread runnable */
  thread->state = _ST_ST_RUNNABLE;
  m_st_active_count++;
  _ST_ADD_RUNQ_NEW(this, thread);
#ifdef DEBUG
  _ST_ADD_THREADQ_NEW(this, thread);
#endif

  return thread;
}

void CStVp::st_thread_main(void)
{
  _st_thread_t *thread = _ST_CURRENT_THREAD_NEW(this);

  /*
   * Cap the stack by zeroing out the saved return address register
   * value. This allows some debugging/profiling tools to know when
   * to stop unwinding the stack. It's a no-op on most platforms.
   */
  MD_CAP_STACK(&thread);

  /* Run thread main */
  thread->retval = (*thread->start)(thread->arg);

  /* All done, time to go away */
  st_thread_exit(thread->retval);
}

void CStVp::st_thread_interrupt(_st_thread_t* trd)
{
    /* If thread is already dead */
    if (trd->state == _ST_ST_ZOMBIE) {
        return;
    }
    
    trd->flags |= _ST_FL_INTERRUPT;
    
    if (trd->state == _ST_ST_RUNNING || trd->state == _ST_ST_RUNNABLE) {
        return;
    }
    
    if (trd->flags & _ST_FL_ON_SLEEPQ) {
        _ST_DEL_SLEEPQ_NEW(this, trd);
    }
    
    /* Make thread runnable */
    trd->state = _ST_ST_RUNNABLE;
    _ST_ADD_RUNQ_NEW(this, trd);
}


void CStVp::st_thread_exit(void *retval)
{
  _st_thread_t *thread = _ST_CURRENT_THREAD_NEW(this);

  thread->retval = retval;
  m_st_active_count--;
  if (thread->term) {
    /* Put thread on the zombie queue */
    thread->state = _ST_ST_ZOMBIE;
    _ST_ADD_ZOMBIEQ_NEW(this, thread);

    /* Notify on our termination condition variable */
    st_cond_signal(thread->term);

    /* Switch context and come back later */
    _ST_SWITCH_CONTEXT_NEW(this, thread);

    /* Continue the cleanup */
    st_cond_destroy(thread->term);
    thread->term = NULL;
  }

#ifdef DEBUG
  _ST_DEL_THREADQ_NEW(this, thread);
#endif

  if (!(thread->flags & _ST_FL_PRIMORDIAL))
    st_stack_free(thread->stack);

  /* Find another thread to run */
  _ST_SWITCH_CONTEXT_NEW(this, thread);
  /* Not going to land here */
}

				   
int CStVp::st_poll(struct pollfd *pds, int npds, st_utime_t timeout)
{
  struct pollfd *pd;
  struct pollfd *epd = pds + npds;
  _st_pollq_t pq;
  _st_thread_t *me = m_curr_thread;
  int n;

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  if (m_io->m_event->st_epoll_pollset_add(pds, npds) < 0)
    return -1;

  pq.pds = pds;
  pq.npds = npds;
  pq.thread = me;
  pq.on_ioq = 1;
  _ST_ADD_IOQ_NEW(this, pq);
  if (timeout != ST_UTIME_NO_TIMEOUT)
    _ST_ADD_SLEEPQ_NEW(this, me, timeout);
  me->state = _ST_ST_IO_WAIT;

  _ST_SWITCH_CONTEXT_NEW(this, me);

  n = 0;
  if (pq.on_ioq) {
    /* If we timed out, the pollq might still be on the ioq. Remove it */
    _ST_DEL_IOQ_NEW(this, pq);
    m_io->m_event->st_epoll_pollset_del(pds, npds);
  } else {
    /* Count the number of ready descriptors */
    for (pd = pds; pd < epd; pd++) {
      if (pd->revents)
	n++;
    }
  }

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  return n;
}

void CStVp::st_vp_schedule(void)
{
  _st_thread_t *thread;

  if (_ST_RUNQ_NEW(this).next != &_ST_RUNQ_NEW(this)) {
    /* Pull thread off of the run queue */
    thread = _ST_THREAD_PTR(_ST_RUNQ_NEW(this).next);
    _ST_DEL_RUNQ_NEW(this,thread);
  } else {
    /* If there are no threads to run, switch to the idle thread */
    thread = idle_thread;
  }
  ST_ASSERT(thread->state == _ST_ST_RUNNABLE);

  /* Resume the thread */
  thread->state = _ST_ST_RUNNING;
  _ST_RESTORE_CONTEXT_NEW(this,thread);
}

void CStVp::st_add_sleep_q(_st_thread_t *trd, st_utime_t timeout)
{
    trd->due = _ST_LAST_CLOCK_NEW(this) + timeout;
    trd->flags |= _ST_FL_ON_SLEEPQ;
    trd->heap_index = ++_ST_SLEEPQ_SIZE_NEW(this);
    heap_insert_new(trd);
}

void CStVp::st_del_sleep_q(_st_thread_t *trd)
{
   	heap_delete_new(trd);
    trd->flags &= ~_ST_FL_ON_SLEEPQ;
}

_st_thread_t **CStVp::heap_insert_new(_st_thread_t *thread) {
  int target = thread->heap_index;
  int s = target;
  _st_thread_t **p = &_ST_SLEEPQ_NEW(this);
  int bits = 0;
  int bit;
  int index = 1;

  while (s) {
    s >>= 1;
    bits++;
  }
  for (bit = bits - 2; bit >= 0; bit--) {
    if (thread->due < (*p)->due) {
      _st_thread_t *t = *p;
      thread->left = t->left;
      thread->right = t->right;
      *p = thread;
      thread->heap_index = index;
      thread = t;
    }
    index <<= 1;
    if (target & (1 << bit)) {
      p = &((*p)->right);
      index |= 1;
    } else {
      p = &((*p)->left);
    }
  }
  thread->heap_index = index;
  *p = thread;
  thread->left = thread->right = NULL;
  return p;
}

void CStVp::heap_delete_new(_st_thread_t *thread) {
  _st_thread_t *t, **p;
  int bits = 0;
  int s, bit;

  /* First find and unlink the last heap element */
  p = &_ST_SLEEPQ_NEW(this);
  s = _ST_SLEEPQ_SIZE_NEW(this);
  while (s) {
    s >>= 1;
    bits++;
  }
  for (bit = bits - 2; bit >= 0; bit--) {
    if (_ST_SLEEPQ_SIZE_NEW(this) & (1 << bit)) {
      p = &((*p)->right);
    } else {
      p = &((*p)->left);
    }
  }
  t = *p;
  *p = NULL;
  --_ST_SLEEPQ_SIZE_NEW(this);
  if (t != thread) {
    /*
     * Insert the unlinked last element in place of the element we are deleting
     */
    t->heap_index = thread->heap_index;
    p = heap_insert_new(t);
    t = *p;
    t->left = thread->left;
    t->right = thread->right;

    /*
     * Reestablish the heap invariant.
     */
    for (;;) {
      _st_thread_t *y; /* The younger child */
      int index_tmp;
      if (t->left == NULL)
	break;
      else if (t->right == NULL)
	y = t->left;
      else if (t->left->due < t->right->due)
	y = t->left;
      else
	y = t->right;
      if (t->due > y->due) {
	_st_thread_t *tl = y->left;
	_st_thread_t *tr = y->right;
	*p = y;
	if (y == t->left) {
	  y->left = t;
	  y->right = t->right;
	  p = &y->left;
	} else {
	  y->left = t->left;
	  y->right = t;
	  p = &y->right;
	}
	t->left = tl;
	t->right = tr;
	index_tmp = t->heap_index;
	t->heap_index = y->heap_index;
	y->heap_index = index_tmp;
      } else {
	break;
      }
    }
  }
  thread->left = thread->right = NULL;
}

_st_stack_t* CStVp::st_stack_new(int stack_size)
{
  _st_clist_t *qp;
  _st_stack_t *ts;
  int extra;

  for (qp = m_st_free_stacks.next; qp != &m_st_free_stacks; qp = qp->next) {
    ts = _ST_THREAD_STACK_PTR(qp);
    if (ts->stk_size >= stack_size) {
      /* Found a stack that is big enough */
      ST_REMOVE_LINK(&ts->links);
      m_st_num_free_stacks--;
      ts->links.next = NULL;
      ts->links.prev = NULL;
      return ts;
    }
  }

  /* Make a new thread stack object. */
  if ((ts = (_st_stack_t *)calloc(1, sizeof(_st_stack_t))) == NULL)
    return NULL;
  extra = m_st_randomize_stacks ? _ST_PAGE_SIZE_NEW(this) : 0;
  ts->vaddr_size = stack_size + 2*REDZONE_NEW(this) + extra;
  ts->vaddr = st_new_stk_segment(ts->vaddr_size);
  if (!ts->vaddr) {
    free(ts);
    return NULL;
  }
  ts->stk_size = stack_size;
  ts->stk_bottom = ts->vaddr + REDZONE_NEW(this);
  ts->stk_top = ts->stk_bottom + stack_size;

#ifdef DEBUG
  mprotect(ts->vaddr, REDZONE_NEW(this), PROT_NONE);
  mprotect(ts->stk_top + extra, REDZONE_NEW(this), PROT_NONE);
#endif

  if (extra) {
    long offset = (random() % extra) & ~0xf;

    ts->stk_bottom += offset;
    ts->stk_top += offset;
  }

  return ts;
}

char* CStVp::st_new_stk_segment(int size)
{
#ifdef MALLOC_STACK
  void *vaddr = malloc(size);
#else
  static int zero_fd = -1;
  int mmap_flags = MAP_PRIVATE;
  void *vaddr;

#if defined (MD_USE_SYSV_ANON_MMAP)
  if (zero_fd < 0) {
    if ((zero_fd = open("/dev/zero", O_RDWR, 0)) < 0)
      return NULL;
    fcntl(zero_fd, F_SETFD, FD_CLOEXEC);
  }
#elif defined (MD_USE_BSD_ANON_MMAP)
  mmap_flags |= MAP_ANON;
#else
#error Unknown OS
#endif

  vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, zero_fd, 0);
  if (vaddr == (void *)MAP_FAILED)
    return NULL;

#endif /* MALLOC_STACK */

  return (char *)vaddr;
}

void CStVp::st_stack_free(_st_stack_t *ts)
{
  if (!ts)
    return;

  /* Put the stack on the free list */
  ST_APPEND_LINK(&ts->links, m_st_free_stacks.prev);
  m_st_num_free_stacks++;
}


_st_cond_t *CStVp::st_cond_new(void)
{
  _st_cond_t *cvar;

  cvar = (_st_cond_t *) calloc(1, sizeof(_st_cond_t));
  if (cvar) {
    ST_INIT_CLIST(&cvar->wait_q);
  }

  return cvar;
}


int CStVp::st_cond_destroy(_st_cond_t *cvar)
{
  if (cvar->wait_q.next != &cvar->wait_q) {
    errno = EBUSY;
    return -1;
  }

  free(cvar);

  return 0;
}


int CStVp::st_cond_timedwait(_st_cond_t *cvar, st_utime_t timeout)
{
  _st_thread_t *me = _ST_CURRENT_THREAD_NEW(this);
  int rv;

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  /* Put caller thread on the condition variable's wait queue */
  me->state = _ST_ST_COND_WAIT;
  ST_APPEND_LINK(&me->wait_links, &cvar->wait_q);

  if (timeout != ST_UTIME_NO_TIMEOUT)
    _ST_ADD_SLEEPQ_NEW(this, me, timeout);

  _ST_SWITCH_CONTEXT_NEW(this, me);

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


int CStVp::st_cond_wait(_st_cond_t *cvar)
{
  return st_cond_timedwait(cvar, ST_UTIME_NO_TIMEOUT);
}


int CStVp::_st_cond_signal(_st_cond_t *cvar, int broadcast)
{
  _st_thread_t *thread;
  _st_clist_t *q;

  for (q = cvar->wait_q.next; q != &cvar->wait_q; q = q->next) {
    thread = _ST_THREAD_WAITQ_PTR(q);
    if (thread->state == _ST_ST_COND_WAIT) {
      if (thread->flags & _ST_FL_ON_SLEEPQ)
	_ST_DEL_SLEEPQ_NEW(this, thread);

      /* Make thread runnable */
      thread->state = _ST_ST_RUNNABLE;
      _ST_ADD_RUNQ_NEW(this, thread);
      if (!broadcast)
	break;
    }
  }

  return 0;
}


int CStVp::st_cond_signal(_st_cond_t *cvar)
{
  return _st_cond_signal(cvar, 0);
}


int CStVp::st_cond_broadcast(_st_cond_t *cvar)
{
  return _st_cond_signal(cvar, 1);
}






