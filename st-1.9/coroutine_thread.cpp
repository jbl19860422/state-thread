#include "coroutine_thread.h"

CCoroutineThreadContainer::CCoroutineThreadContainer()
{
	_thread = NULL;
	_st_curr_time = 0;
	_st_active_count = 0;
	_st_curr_time = 0;
	key_max = 0;
	_st_num_free_stacks = 0;
	_st_randomize_stacks = 0;
	zero_fd = -1;
	_st_free_stacks = ST_INIT_STATIC_CLIST(&_st_free_stacks);
	_st_osfd_limit = -1;
	_st_utime = NULL;
	_st_epoll_eventsys.name = "epoll";
	_st_epoll_eventsys.val = ST_EVENTSYS_ALT;
	_st_epoll_eventsys.init = _st_epoll_init_new;
	_st_epoll_eventsys.dispatch = _st_epoll_dispatch_new;
	_st_epoll_eventsys.fd_new = _st_epoll_fd_new_new;
	_st_epoll_eventsys.fd_close = _st_epoll_fd_close_new;
	_st_epoll_eventsys.fd_getlimit = _st_epoll_fd_getlimit_new;
	_st_epoll_eventsys.pollset_add = _st_epoll_pollset_add_new;
	_st_epoll_eventsys.pollset_del = _st_epoll_pollset_del_new;
}

#ifdef DEBUG
void CCoroutineThreadContainer::_st_show_thread_stack(_st_thread_t *thread, const char *messg)
{

}

void CCoroutineThreadContainer::_st_iterate_threads(void)
{
  static jmp_buf orig_jb, save_jb;
  _st_clist_t *q;

  if (!_st_iterate_threads_flag) {
    if (_thread) {
      memcpy(_thread->context, save_jb, sizeof(jmp_buf));
      MD_LONGJMP(orig_jb, 1);
    }
    return;
  }

  if (_thread) {
    memcpy(_thread->context, save_jb, sizeof(jmp_buf));
    _st_show_thread_stack(_thread, NULL);
  } else {
    if (MD_SETJMP(orig_jb)) {
      _st_iterate_threads_flag = 0;
      _thread = NULL;
      _st_show_thread_stack(_thread, "Iteration completed");
      return;
    }
    _thread = _ST_CURRENT_THREAD_NEW(this);
    _st_show_thread_stack(_thread, "Iteration started");
  }

  q = _thread->tlink.next;
  if (q == &_ST_THREADQ_NEW(this))
    q = q->next;
  ST_ASSERT(q != &_ST_THREADQ_NEW(this));
  _thread = _ST_THREAD_THREADQ_PTR(q);
  if (_thread == _ST_CURRENT_THREAD_NEW(this))
    MD_LONGJMP(orig_jb, 1);
  memcpy(save_jb, _thread->context, sizeof(jmp_buf));
  MD_LONGJMP(_thread->context, 1);
}
#endif

void CCoroutineThreadContainer::_st_vp_schedule(void)
{
  _st_thread_t *thread;

  if (_ST_RUNQ_NEW(this).next != &_ST_RUNQ_NEW(this)) {
	/* Pull thread off of the run queue */
	thread = _ST_THREAD_PTR(_ST_RUNQ_NEW(this).next);
	_ST_DEL_RUNQ(thread);
  } else {
	/* If there are no threads to run, switch to the idle thread */
	thread = _st_this_vp.idle_thread;
  }
  ST_ASSERT(thread->state == _ST_ST_RUNNABLE);

  /* Resume the thread */
  thread->state = _ST_ST_RUNNING;
  _ST_RESTORE_CONTEXT_NEW(this, thread);
}

void CCoroutineThreadContainer::_st_vp_check_clock()
{
  _st_thread_t *thread;
  st_utime_t elapsed, now;
 
  now = st_utime();
  elapsed = now - _ST_LAST_CLOCK;
  _ST_LAST_CLOCK = now;

  if (_st_curr_time && now - _st_last_tset > 999000) {
    _st_curr_time = time(NULL);
    _st_last_tset = now;
  }

  while (_ST_SLEEPQ != NULL) {
    thread = _ST_SLEEPQ;
    ST_ASSERT(thread->flags & _ST_FL_ON_SLEEPQ);
    if (thread->due > now)
      break;
    _ST_DEL_SLEEPQ_NEW(thread);

    /* If thread is waiting on condition variable, set the time out flag */
    if (thread->state == _ST_ST_COND_WAIT)
      thread->flags |= _ST_FL_TIMEDOUT;

    /* Make thread runnable */
    ST_ASSERT(!(thread->flags & _ST_FL_IDLE_THREAD));
    thread->state = _ST_ST_RUNNABLE;
    _ST_ADD_RUNQ_NEW(this,thread);
  }
}

/*
 * Start function for the idle thread
 */
/* ARGSUSED */
void* CCoroutineThreadContainer::_st_idle_thread_start(void *arg)
{
  CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)arg;
  return container->st_idle_thread_start();
}

void* CCoroutineThreadContainer::st_idle_thread_start()
{
  _st_thread_t *me = _ST_CURRENT_THREAD_NEW(this);

  while (_st_active_count > 0) {
    /* Idle vp till I/O is ready or the smallest timeout expired */
    _ST_VP_IDLE_NEW(this);

    /* Check sleep queue for expired threads */
    _st_vp_check_clock();

    me->state = _ST_ST_RUNNABLE;
    //_ST_SWITCH_CONTEXT_NEW(this, me);//del error
  }

  /* No more threads */
  exit(0);

  /* NOTREACHED */
  return NULL;
}


void CCoroutineThreadContainer::_st_thread_main(void)
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

void CCoroutineThreadContainer::st_thread_exit(void *retval)
{
  _st_thread_t *thread = _ST_CURRENT_THREAD_NEW(this);

  thread->retval = retval;
  _st_thread_cleanup(thread);
  _st_active_count--;
  if (thread->term) {
    /* Put thread on the zombie queue */
    thread->state = _ST_ST_ZOMBIE;
    _ST_ADD_ZOMBIEQ_NEW(this,thread);

    /* Notify on our termination condition variable */
    st_cond_signal(thread->term);

    /* Switch context and come back later */
    //_ST_SWITCH_CONTEXT_NEW(this, thread);//del error

    /* Continue the cleanup */
    st_cond_destroy(thread->term);
    thread->term = NULL;
  }
}

void CCoroutineThreadContainer::_st_thread_cleanup(_st_thread_t *thread)
{
  int key;

  for (key = 0; key < key_max; key++) {
    if (thread->private_data[key] && _st_destructors[key]) {
      (*_st_destructors[key])(thread->private_data[key]);
      thread->private_data[key] = NULL;
    }
  }
}

/*
 * Insert "thread" into the timeout heap, in the position
 * specified by thread->heap_index.  See docs/timeout_heap.txt
 * for details about the timeout heap.
 */
_st_thread_t** CCoroutineThreadContainer::heap_insert(_st_thread_t *thread) {
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

/*
 * Delete "thread" from the timeout heap.
 */
void CCoroutineThreadContainer::heap_delete(_st_thread_t *thread) {
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
    p = heap_insert(t);
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



void CCoroutineThreadContainer::_st_add_sleep_q(_st_thread_t *thread, st_utime_t timeout)
{
  thread->due = _ST_LAST_CLOCK_NEW(this) + timeout;
  thread->flags |= _ST_FL_ON_SLEEPQ;
  thread->heap_index = ++_ST_SLEEPQ_SIZE;
  heap_insert(thread);
}


void CCoroutineThreadContainer::_st_del_sleep_q(_st_thread_t *thread)
{
  heap_delete(thread);
  thread->flags &= ~_ST_FL_ON_SLEEPQ;
}

_st_stack_t* CCoroutineThreadContainer::_st_stack_new(int stack_size)
{
  _st_clist_t *qp;
  _st_stack_t *ts;
  int extra;

  for (qp = _st_free_stacks.next; qp != &_st_free_stacks; qp = qp->next) {
    ts = _ST_THREAD_STACK_PTR(qp);
    if (ts->stk_size >= stack_size) {
      /* Found a stack that is big enough */
      ST_REMOVE_LINK(&ts->links);
      _st_num_free_stacks--;
      ts->links.next = NULL;
      ts->links.prev = NULL;
      return ts;
    }
  }

  /* Make a new thread stack object. */
  if ((ts = (_st_stack_t *)calloc(1, sizeof(_st_stack_t))) == NULL)
    return NULL;
  extra = _st_randomize_stacks ? _ST_PAGE_SIZE : 0;
  ts->vaddr_size = stack_size + 2*REDZONE + extra;
  ts->vaddr = _st_new_stk_segment(ts->vaddr_size);
  if (!ts->vaddr) {
    free(ts);
    return NULL;
  }
  ts->stk_size = stack_size;
  ts->stk_bottom = ts->vaddr + REDZONE;
  ts->stk_top = ts->stk_bottom + stack_size;

#ifdef DEBUG
  mprotect(ts->vaddr, REDZONE, PROT_NONE);
  mprotect(ts->stk_top + extra, REDZONE, PROT_NONE);
#endif

  if (extra) {
    long offset = (random() % extra) & ~0xf;

    ts->stk_bottom += offset;
    ts->stk_top += offset;
  }

  return ts;
}

char* CCoroutineThreadContainer::_st_new_stk_segment(int size)
{
#ifdef MALLOC_STACK
  void *vaddr = malloc(size);
#else
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

/*
 * Free the stack for the current thread
 */
void CCoroutineThreadContainer::_st_stack_free(_st_stack_t *ts)
{
  if (!ts)
    return;

  /* Put the stack on the free list */
  ST_APPEND_LINK(&ts->links, _st_free_stacks.prev);
  _st_num_free_stacks++;
}

int CCoroutineThreadContainer::_st_io_init(void)
{
  struct sigaction sigact;
  struct rlimit rlim;
  int fdlim;

  /* Ignore SIGPIPE */
  sigact.sa_handler = SIG_IGN;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  if (sigaction(SIGPIPE, &sigact, NULL) < 0)
    return -1;

  /* Set maximum number of open file descriptors */
  if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;

  fdlim = (*_st_eventsys->fd_getlimit)(this);
  if (fdlim > 0 && rlim.rlim_max > (rlim_t) fdlim) {
    rlim.rlim_max = fdlim;
  }

  /* when rlimit max is negative, for example, osx, use cur directly. */
  /* @see https://github.com/winlinvip/simple-rtmp-server/issues/336 */
  if ((int)rlim.rlim_max < 0) {
    _st_osfd_limit = (int)(fdlim > 0? fdlim : rlim.rlim_cur);
    return 0;
  }

  rlim.rlim_cur = rlim.rlim_max;
  if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;
  _st_osfd_limit = (int) rlim.rlim_max;

  return 0;
}

st_utime_t CCoroutineThreadContainer::st_utime(void)
{
  if (_st_utime == NULL) {
#ifdef MD_GET_UTIME
    MD_GET_UTIME();
#else
#error Unknown OS
#endif
  }

  return (*_st_utime)();
}

int CCoroutineThreadContainer::st_set_utime_function(st_utime_t (*func)(void))
{
  if (_st_active_count) {
    errno = EINVAL;
    return -1;
  }

  _st_utime = func;

  return 0;
}

_st_cond_t* CCoroutineThreadContainer::st_cond_new(void)
{
  _st_cond_t *cvar;

  cvar = (_st_cond_t *) calloc(1, sizeof(_st_cond_t));
  if (cvar) {
    ST_INIT_CLIST(&cvar->wait_q);
  }

  return cvar;
}

int CCoroutineThreadContainer::st_cond_destroy(_st_cond_t *cvar)
{
  if (cvar->wait_q.next != &cvar->wait_q) {
    errno = EBUSY;
    return -1;
  }

  free(cvar);

  return 0;
}

int CCoroutineThreadContainer::st_cond_timedwait(_st_cond_t *cvar, st_utime_t timeout)
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
    _ST_ADD_SLEEPQ_NEW(me, timeout);

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

int CCoroutineThreadContainer::st_cond_signal(_st_cond_t *cvar)
{
  return _st_cond_signal(cvar, 0);
}


int CCoroutineThreadContainer::_st_cond_signal(_st_cond_t *cvar, int broadcast)
{
  _st_thread_t *thread;
  _st_clist_t *q;

  for (q = cvar->wait_q.next; q != &cvar->wait_q; q = q->next) {
    thread = _ST_THREAD_WAITQ_PTR(q);
    if (thread->state == _ST_ST_COND_WAIT) {
      if (thread->flags & _ST_FL_ON_SLEEPQ)
		_ST_DEL_SLEEPQ_NEW(thread);

      /* Make thread runnable */
      thread->state = _ST_ST_RUNNABLE;
      _ST_ADD_RUNQ_NEW(this,thread);
      if (!broadcast)
	break;
    }
  }

  return 0;
}

ssize_t CCoroutineThreadContainer::st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout)
{
  ssize_t n;

  while ((n = read(fd->osfd, buf, nbyte)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return n;
}

/*
 * Wait for I/O on a single descriptor.
 */
int CCoroutineThreadContainer::st_netfd_poll(_st_netfd_t *fd, int how, st_utime_t timeout)
{
  struct pollfd pd;
  int n;

  pd.fd = fd->osfd;
  pd.events = (short) how;
  pd.revents = 0;

  if ((n = st_poll(&pd, 1, timeout)) < 0)
    return -1;
  if (n == 0) {
    /* Timed out */
    errno = ETIME;
    return -1;
  }
  if (pd.revents & POLLNVAL) {
    errno = EBADF;
    return -1;
  }

  return 0;
}

int CCoroutineThreadContainer::st_poll(struct pollfd *pds, int npds, st_utime_t timeout)
{
  struct pollfd *pd;
  struct pollfd *epd = pds + npds;
  _st_pollq_t pq;
  _st_thread_t *me = _ST_CURRENT_THREAD_NEW(this);
  int n;

  if (me->flags & _ST_FL_INTERRUPT) {
    me->flags &= ~_ST_FL_INTERRUPT;
    errno = EINTR;
    return -1;
  }

  if ((*_st_eventsys->pollset_add)(this,pds, npds) < 0)
    return -1;

  pq.pds = pds;
  pq.npds = npds;
  pq.thread = me;
  pq.on_ioq = 1;
  _ST_ADD_IOQ_NEW(this, pq);
  if (timeout != ST_UTIME_NO_TIMEOUT)
    _ST_ADD_SLEEPQ_NEW(me, timeout);
  me->state = _ST_ST_IO_WAIT;

  _ST_SWITCH_CONTEXT_NEW(this, me);

  n = 0;
  if (pq.on_ioq) {
    /* If we timed out, the pollq might still be on the ioq. Remove it */
    _ST_DEL_IOQ_NEW(pq);
    (*_st_eventsys->pollset_del)(this,pds, npds);
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

_st_thread_t *CCoroutineThreadContainer::st_thread_create(void *(*start)(void *arg), void *arg,
			       int joinable, int stk_size)
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
  stk_size = ((stk_size + _ST_PAGE_SIZE - 1) / _ST_PAGE_SIZE) * _ST_PAGE_SIZE;
  stack = _st_stack_new(stk_size);
  if (!stack)
    return NULL;

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
  _ST_INIT_CONTEXT(thread, stack->sp, _st_thread_main);
#else
  _ST_INIT_CONTEXT(thread, stack->sp, stack->bsp, _st_thread_main);
#endif

  /* If thread is joinable, allocate a termination condition variable */
  if (joinable) {
    thread->term = st_cond_new();
    if (thread->term == NULL) {
      _st_stack_free(thread->stack);
      return NULL;
    }
  }

  /* Make thread runnable */
  thread->state = _ST_ST_RUNNABLE;
  _st_active_count++;
  _ST_ADD_RUNQ_NEW(this,thread);
#ifdef DEBUG
  _ST_ADD_THREADQ_NEW(this, thread);
#endif

  return thread;
}


int CCoroutineThreadContainer::st_thread_join(_st_thread_t *thread, void **retvalp)
{
  _st_cond_t *term = thread->term;

  /* Can't join a non-joinable thread */
  if (term == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (_ST_CURRENT_THREAD_NEW(this) == thread) {
    errno = EDEADLK;
    return -1;
  }

  /* Multiple threads can't wait on the same joinable thread */
  if (term->wait_q.next != &term->wait_q) {
    errno = EINVAL;
    return -1;
  }

  while (thread->state != _ST_ST_ZOMBIE) {
    if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0)
      return -1;
  }

  if (retvalp)
    *retvalp = thread->retval;

  /*
   * Remove target thread from the zombie queue and make it runnable.
   * When it gets scheduled later, it will do the clean up.
   */
  thread->state = _ST_ST_RUNNABLE;
  _ST_DEL_ZOMBIEQ_NEW(thread);
  _ST_ADD_RUNQ_NEW(this, thread);

  return 0;
}

st_switch_cb_t CCoroutineThreadContainer::st_set_switch_in_cb(st_switch_cb_t cb)
{
  st_switch_cb_t ocb = _st_this_vp.switch_in_cb;
  _st_this_vp.switch_in_cb = cb;
  return ocb;
}

st_switch_cb_t CCoroutineThreadContainer::st_set_switch_out_cb(st_switch_cb_t cb)
{
  st_switch_cb_t ocb = _st_this_vp.switch_out_cb;
  _st_this_vp.switch_out_cb = cb;
  return ocb;
}

/*
 * Check if epoll functions are just stubs.
 */
int CCoroutineThreadContainer::_st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}

int CCoroutineThreadContainer::st_set_eventsys(int eventsys)
{
    if (_st_eventsys) {
        errno = EBUSY;
        return -1;
    }

    switch (eventsys) {
    case ST_EVENTSYS_DEFAULT:
    case ST_EVENTSYS_ALT:
    default:
        if (_st_epoll_is_supported()) {
            _st_eventsys = &_st_epoll_eventsys;
            break;
        }
        errno = EINVAL;
        return -1;
    }

    return 0;
}


int CCoroutineThreadContainer::st_init(void)
{
  _st_thread_t *thread;

  if (_st_active_count) {
    /* Already initialized */
    return 0;
  }

  /* We can ignore return value here */
  st_set_eventsys(ST_EVENTSYS_DEFAULT);

  if (_st_io_init() < 0)
    return -1;

  memset(&_st_this_vp, 0, sizeof(_st_vp_t));

  ST_INIT_CLIST(&_ST_RUNQ_NEW(this));
  ST_INIT_CLIST(&_ST_IOQ_NEW(this));
  ST_INIT_CLIST(&_ST_ZOMBIEQ_NEW(this));
#ifdef DEBUG
  ST_INIT_CLIST(&_ST_THREADQ_NEW(this));
#endif

  if ((*_st_eventsys->init)(this) < 0)
    return -1;

  _st_this_vp.pagesize = getpagesize();
  _st_this_vp.last_clock = st_utime();

  /*
   * Create idle thread
   */
  _st_this_vp.idle_thread = st_thread_create(_st_idle_thread_start,
					     this, 0, 0);
  if (!_st_this_vp.idle_thread)
    return -1;
  _st_this_vp.idle_thread->flags = _ST_FL_IDLE_THREAD;
  _st_active_count--;
  _ST_DEL_RUNQ_NEW(_st_this_vp.idle_thread);

  /*
   * Initialize primordial thread
   */
  thread = (_st_thread_t *) calloc(1, sizeof(_st_thread_t) +
				   (ST_KEYS_MAX * sizeof(void *)));
  if (!thread)
    return -1;
  thread->private_data = (void **) (thread + 1);
  thread->state = _ST_ST_RUNNING;
  thread->flags = _ST_FL_PRIMORDIAL;
  _ST_SET_CURRENT_THREAD_NEW(this, thread);
  _st_active_count++;
#ifdef DEBUG
  _ST_ADD_THREADQ_NEW(this,thread);
#endif

  return 0;
}



int CCoroutineThreadContainer::_st_epoll_init_new(void* p)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_init();
}

void CCoroutineThreadContainer::_st_epoll_dispatch_new(void* p)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_dispatch();
}

int CCoroutineThreadContainer::_st_epoll_pollset_add_new(void* p,struct pollfd *pds, int npds)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_pollset_add(pds,npds);
}

void CCoroutineThreadContainer::_st_epoll_pollset_del_new(void* p,struct pollfd *pds, int npds)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	container->st_epoll_pollset_del(pds,npds);
}

int CCoroutineThreadContainer::_st_epoll_fd_new_new(void* p,int osfd)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_fd_new(osfd);
}

int CCoroutineThreadContainer::_st_epoll_fd_close_new(void* p,int osfd)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_fd_close(osfd);
}

int CCoroutineThreadContainer::_st_epoll_fd_getlimit_new(void* p)
{
	CCoroutineThreadContainer* container = (CCoroutineThreadContainer*)p;
	return container->st_epoll_fd_getlimit();
}


int CCoroutineThreadContainer::st_epoll_init()
{
    int fdlim;
    int err = 0;
    int rv = 0;

    _st_epoll_data =
        (struct _st_epolldata *) calloc(1, sizeof(struct _st_epolldata));
    if (!_st_epoll_data)
        return -1;

    fdlim = st_getfdlimit();
    _st_epoll_data->fd_hint = (fdlim > 0 && fdlim < ST_EPOLL_EVTLIST_SIZE) ?
        fdlim : ST_EPOLL_EVTLIST_SIZE;

    if ((_st_epoll_data->epfd = epoll_create(_st_epoll_data->fd_hint)) < 0) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }
    fcntl(_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);
    _st_epoll_data->pid = getpid();

    /* Allocate file descriptor data array */
    _st_epoll_data->fd_data_size = _st_epoll_data->fd_hint;
    _st_epoll_data->fd_data =
        (_epoll_fd_data_t *)calloc(_st_epoll_data->fd_data_size,
                                   sizeof(_epoll_fd_data_t));
    if (!_st_epoll_data->fd_data) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }

    /* Allocate event lists */
    _st_epoll_data->evtlist_size = _st_epoll_data->fd_hint;
    _st_epoll_data->evtlist =
        (struct epoll_event *)malloc(_st_epoll_data->evtlist_size *
                                     sizeof(struct epoll_event));
    if (!_st_epoll_data->evtlist) {
        err = errno;
        rv = -1;
    }

 cleanup_epoll:
    if (rv < 0) {
        if (_st_epoll_data->epfd >= 0)
            close(_st_epoll_data->epfd);
        free(_st_epoll_data->fd_data);
        free(_st_epoll_data->evtlist);
        free(_st_epoll_data);
        _st_epoll_data = NULL;
        errno = err;
    }

    return rv;
}

void CCoroutineThreadContainer::st_epoll_dispatch(void)
{
    st_utime_t min_timeout;
    _st_clist_t *q;
    _st_pollq_t *pq;
    struct pollfd *pds, *epds;
    struct epoll_event ev;
    int timeout, nfd, i, osfd, notify;
    int events, op;
    short revents;

    if (_ST_SLEEPQ_NEW(this) == NULL) {
        timeout = -1;
    } else {
        min_timeout = (_ST_SLEEPQ_NEW(this)->due <= _ST_LAST_CLOCK_NEW(this)) ? 0 :
            (_ST_SLEEPQ_NEW(this)->due - _ST_LAST_CLOCK_NEW(this));
        timeout = (int) (min_timeout / 1000);
    }

    if (_st_epoll_data->pid != getpid()) {
        /* We probably forked, reinitialize epoll set */
        close(_st_epoll_data->epfd);
        _st_epoll_data->epfd = epoll_create(_st_epoll_data->fd_hint);
        if (_st_epoll_data->epfd < 0) {
            /* There is nothing we can do here, will retry later */
            return;
        }
        fcntl(_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);
        _st_epoll_data->pid = getpid();

        /* Put all descriptors on ioq into new epoll set */
        memset(_st_epoll_data->fd_data, 0,
               _st_epoll_data->fd_data_size * sizeof(_epoll_fd_data_t));
        _st_epoll_data->evtlist_cnt = 0;
        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            st_epoll_pollset_add(pq->pds, pq->npds);
        }
    }

    /* Check for I/O operations */
    nfd = epoll_wait(_st_epoll_data->epfd, _st_epoll_data->evtlist,
                     _st_epoll_data->evtlist_size, timeout);

    if (nfd > 0) {
        for (i = 0; i < nfd; i++) {
            osfd = _st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS(osfd) = _st_epoll_data->evtlist[i].events;
            if (_ST_EPOLL_REVENTS(osfd) & (EPOLLERR | EPOLLHUP)) {
                /* Also set I/O bits on error */
                _ST_EPOLL_REVENTS(osfd) |= _ST_EPOLL_EVENTS(osfd);
            }
        }

        for (q = _ST_IOQ.next; q != &_ST_IOQ; q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;

            for (pds = pq->pds; pds < epds; pds++) {
                if (_ST_EPOLL_REVENTS(pds->fd) == 0) {
                    pds->revents = 0;
                    continue;
                }
                osfd = pds->fd;
                events = pds->events;
                revents = 0;
                if ((events & POLLIN) && (_ST_EPOLL_REVENTS(osfd) & EPOLLIN))
                    revents |= POLLIN;
                if ((events & POLLOUT) && (_ST_EPOLL_REVENTS(osfd) & EPOLLOUT))
                    revents |= POLLOUT;
                if ((events & POLLPRI) && (_ST_EPOLL_REVENTS(osfd) & EPOLLPRI))
                    revents |= POLLPRI;
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLERR)
                    revents |= POLLERR;
                if (_ST_EPOLL_REVENTS(osfd) & EPOLLHUP)
                    revents |= POLLHUP;

                pds->revents = revents;
                if (revents) {
                    notify = 1;
                }
            }
            if (notify) {
                ST_REMOVE_LINK(&pq->links);
                pq->on_ioq = 0;
                /*
                 * Here we will only delete/modify descriptors that
                 * didn't fire (see comments in _st_epoll_pollset_del()).
                 */
                st_epoll_pollset_del(pq->pds, pq->npds);

                if (pq->thread->flags & _ST_FL_ON_SLEEPQ)
                    _ST_DEL_SLEEPQ_NEW(pq->thread);
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ_NEW(this, pq->thread);
            }
        }

        for (i = 0; i < nfd; i++) {
            /* Delete/modify descriptors that fired */
            osfd = _st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS(osfd) = 0;
            events = _ST_EPOLL_EVENTS(osfd);
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = osfd;
            if (epoll_ctl(_st_epoll_data->epfd, op, osfd, &ev) == 0 &&
                op == EPOLL_CTL_DEL) {
                _st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

int CCoroutineThreadContainer::st_epoll_fd_new(int osfd)
{
    if (osfd >= _st_epoll_data->fd_data_size &&
        st_epoll_fd_data_expand(osfd) < 0)
        return -1;

    return 0;   
}

int CCoroutineThreadContainer::st_epoll_fd_data_expand(int maxfd)
{
    _epoll_fd_data_t *ptr;
    int n = _st_epoll_data->fd_data_size;

    while (maxfd >= n)
        n <<= 1;

    ptr = (_epoll_fd_data_t *)realloc(_st_epoll_data->fd_data,
                                      n * sizeof(_epoll_fd_data_t));
    if (!ptr)
        return -1;

    memset(ptr + _st_epoll_data->fd_data_size, 0,
           (n - _st_epoll_data->fd_data_size) * sizeof(_epoll_fd_data_t));

    _st_epoll_data->fd_data = ptr;
    _st_epoll_data->fd_data_size = n;

    return 0;
}

void CCoroutineThreadContainer::st_epoll_evtlist_expand(void)
{
    struct epoll_event *ptr;
    int n = _st_epoll_data->evtlist_size;

    while (_st_epoll_data->evtlist_cnt > n)
        n <<= 1;

    ptr = (struct epoll_event *)realloc(_st_epoll_data->evtlist,
                                        n * sizeof(struct epoll_event));
    if (ptr) {
        _st_epoll_data->evtlist = ptr;
        _st_epoll_data->evtlist_size = n;
    }
}

void CCoroutineThreadContainer::st_epoll_pollset_del(struct pollfd *pds, int npds)
{
    struct epoll_event ev;
    struct pollfd *pd;
    struct pollfd *epd = pds + npds;
    int old_events, events, op;

    /*
     * It's more or less OK if deleting fails because a descriptor
     * will either be closed or deleted in dispatch function after
     * it fires.
     */
    for (pd = pds; pd < epd; pd++) {
        old_events = _ST_EPOLL_EVENTS(pd->fd);

        if (pd->events & POLLIN)
            _ST_EPOLL_READ_CNT(pd->fd)--;
        if (pd->events & POLLOUT)
            _ST_EPOLL_WRITE_CNT(pd->fd)--;
        if (pd->events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT(pd->fd)--;

        events = _ST_EPOLL_EVENTS(pd->fd);
        /*
         * The _ST_EPOLL_REVENTS check below is needed so we can use
         * this function inside dispatch(). Outside of dispatch()
         * _ST_EPOLL_REVENTS is always zero for all descriptors.
         */
        if (events != old_events && _ST_EPOLL_REVENTS(pd->fd) == 0) {
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = pd->fd;
            if (epoll_ctl(_st_epoll_data->epfd, op, pd->fd, &ev) == 0 &&
                op == EPOLL_CTL_DEL) {
                _st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

int CCoroutineThreadContainer::st_epoll_pollset_add(struct pollfd *pds, int npds)
{
    struct epoll_event ev;
    int i, fd;
    int old_events, events, op;

    /* Do as many checks as possible up front */
    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        if (fd < 0 || !pds[i].events ||
            (pds[i].events & ~(POLLIN | POLLOUT | POLLPRI))) {
            errno = EINVAL;
            return -1;
        }
        if (fd >= _st_epoll_data->fd_data_size &&
            st_epoll_fd_data_expand(fd) < 0)
            return -1;
    }

    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        old_events = _ST_EPOLL_EVENTS(fd);

        if (pds[i].events & POLLIN)
            _ST_EPOLL_READ_CNT(fd)++;
        if (pds[i].events & POLLOUT)
            _ST_EPOLL_WRITE_CNT(fd)++;
        if (pds[i].events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT(fd)++;

        events = _ST_EPOLL_EVENTS(fd);
        if (events != old_events) {
            op = old_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
            ev.events = events;
            ev.data.fd = fd;
            if (epoll_ctl(_st_epoll_data->epfd, op, fd, &ev) < 0 &&
                (op != EPOLL_CTL_ADD || errno != EEXIST))
                break;
            if (op == EPOLL_CTL_ADD) {
                _st_epoll_data->evtlist_cnt++;
                if (_st_epoll_data->evtlist_cnt > _st_epoll_data->evtlist_size)
                    st_epoll_evtlist_expand();
            }
        }
    }

    if (i < npds) {
        /* Error */
        int err = errno;
        /* Unroll the state */
        st_epoll_pollset_del(pds, i + 1);
        errno = err;
        return -1;
    }

    return 0;
}

int CCoroutineThreadContainer::st_epoll_fd_close(int osfd)
{
    if (_ST_EPOLL_READ_CNT(osfd) || _ST_EPOLL_WRITE_CNT(osfd) ||
        _ST_EPOLL_EXCEP_CNT(osfd)) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

int CCoroutineThreadContainer::st_epoll_fd_getlimit(void)
{
    /* zero means no specific limit */
    return 0;
}












