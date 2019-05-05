#include "st_event.h"
#include "stdio.h"
int CStEvent::st_epoll_init() 
{
	int fdlim;
    int err = 0;
    int rv = 0;

    m_st_epoll_data =
        (struct _st_epolldata *) calloc(1, sizeof(struct _st_epolldata));
    if (!m_st_epoll_data)
        return -1;

    fdlim = st_getfdlimit();
    m_st_epoll_data->fd_hint = (fdlim > 0 && fdlim < ST_EPOLL_EVTLIST_SIZE) ?
        fdlim : ST_EPOLL_EVTLIST_SIZE;

    if ((m_st_epoll_data->epfd = epoll_create(m_st_epoll_data->fd_hint)) < 0) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }
    fcntl(m_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);
    m_st_epoll_data->pid = getpid();

    /* Allocate file descriptor data array */
    m_st_epoll_data->fd_data_size = m_st_epoll_data->fd_hint;
    m_st_epoll_data->fd_data =
        (_epoll_fd_data_t *)calloc(m_st_epoll_data->fd_data_size,
                                   sizeof(_epoll_fd_data_t));
    if (!m_st_epoll_data->fd_data) {
        err = errno;
        rv = -1;
        goto cleanup_epoll;
    }

    /* Allocate event lists */
    m_st_epoll_data->evtlist_size = m_st_epoll_data->fd_hint;
    m_st_epoll_data->evtlist =
        (struct epoll_event *)malloc(m_st_epoll_data->evtlist_size *
                                     sizeof(struct epoll_event));
    if (!m_st_epoll_data->evtlist) {
        err = errno;
        rv = -1;
    }

 cleanup_epoll:
    if (rv < 0) {
        if (m_st_epoll_data->epfd >= 0)
            close(m_st_epoll_data->epfd);
        free(m_st_epoll_data->fd_data);
        free(m_st_epoll_data->evtlist);
        free(m_st_epoll_data);
        m_st_epoll_data = NULL;
        errno = err;
    }
	printf("st_epoll_init sucess rv=%d\n",rv);
	printf("m_st_epoll_data->fd_data_size=%d\n",m_st_epoll_data->fd_data_size);
    return rv;
}


int CStEvent::st_epoll_fd_data_expand(int maxfd)
{
    _epoll_fd_data_t *ptr;
    int n = m_st_epoll_data->fd_data_size;
	printf("m_st_epoll_data->fd_data_size=%d\n",m_st_epoll_data->fd_data_size);
    while (maxfd >= n)
    {
    	static int a = 0;
		if(a < 5) {
			a++;
    		printf("m_st_epoll_data->fd_data_size=%d,\nmaxfd=%d,n=%d,m_st_epoll_data->fd_hint=%d\n",m_st_epoll_data->fd_data_size,maxfd,n,m_st_epoll_data->fd_hint);
		}
		
		n <<= 1;
    }

    ptr = (_epoll_fd_data_t *)realloc(m_st_epoll_data->fd_data,
                                      n * sizeof(_epoll_fd_data_t));
    if (!ptr)
        return -1;

    memset(ptr + m_st_epoll_data->fd_data_size, 0,
           (n - m_st_epoll_data->fd_data_size) * sizeof(_epoll_fd_data_t));

    m_st_epoll_data->fd_data = ptr;
    m_st_epoll_data->fd_data_size = n;

    return 0;
}

void CStEvent::st_epoll_evtlist_expand(void)
{
    struct epoll_event *ptr;
    int n = m_st_epoll_data->evtlist_size;

    while (m_st_epoll_data->evtlist_cnt > n)
        n <<= 1;

    ptr = (struct epoll_event *)realloc(m_st_epoll_data->evtlist,
                                        n * sizeof(struct epoll_event));
    if (ptr) {
        m_st_epoll_data->evtlist = ptr;
        m_st_epoll_data->evtlist_size = n;
    }
}

void CStEvent::st_epoll_pollset_del(struct pollfd *pds, int npds)
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
        old_events = _ST_EPOLL_EVENTS_NEW(pd->fd);

        if (pd->events & POLLIN)
            _ST_EPOLL_READ_CNT_NEW(pd->fd)--;
        if (pd->events & POLLOUT)
            _ST_EPOLL_WRITE_CNT_NEW(pd->fd)--;
        if (pd->events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT_NEW(pd->fd)--;

        events = _ST_EPOLL_EVENTS_NEW(pd->fd);
        /*
         * The _ST_EPOLL_REVENTS check below is needed so we can use
         * this function inside dispatch(). Outside of dispatch()
         * _ST_EPOLL_REVENTS is always zero for all descriptors.
         */
        if (events != old_events && _ST_EPOLL_REVENTS_NEW(pd->fd) == 0) {
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = pd->fd;
            if (epoll_ctl(m_st_epoll_data->epfd, op, pd->fd, &ev) == 0 &&
                op == EPOLL_CTL_DEL) {
                m_st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

int CStEvent::st_epoll_pollset_add(struct pollfd *pds, int npds)
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
        if (fd >= m_st_epoll_data->fd_data_size &&
            st_epoll_fd_data_expand(fd) < 0)
            return -1;
    }

    for (i = 0; i < npds; i++) {
        fd = pds[i].fd;
        old_events = _ST_EPOLL_EVENTS_NEW(fd);

        if (pds[i].events & POLLIN)
            _ST_EPOLL_READ_CNT_NEW(fd)++;
        if (pds[i].events & POLLOUT)
            _ST_EPOLL_WRITE_CNT_NEW(fd)++;
        if (pds[i].events & POLLPRI)
            _ST_EPOLL_EXCEP_CNT_NEW(fd)++;

        events = _ST_EPOLL_EVENTS_NEW(fd);
        if (events != old_events) {
            op = old_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
            ev.events = events;
            ev.data.fd = fd;
            if (epoll_ctl(m_st_epoll_data->epfd, op, fd, &ev) < 0 &&
                (op != EPOLL_CTL_ADD || errno != EEXIST))
                break;
            if (op == EPOLL_CTL_ADD) {
                m_st_epoll_data->evtlist_cnt++;
                if (m_st_epoll_data->evtlist_cnt > m_st_epoll_data->evtlist_size)
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

void CStEvent::st_epoll_dispatch(void)
{
    st_utime_t min_timeout;
    _st_clist_t *q;
    _st_pollq_t *pq;
    struct pollfd *pds, *epds;
    struct epoll_event ev;
    int timeout, nfd, i, osfd, notify;
    int events, op;
    short revents;

    if (_ST_SLEEPQ_NEW(m_vp) == NULL) {
        timeout = -1;
    } else {
        min_timeout = (_ST_SLEEPQ_NEW(m_vp)->due <= _ST_LAST_CLOCK_NEW(m_vp)) ? 0 :
            (_ST_SLEEPQ_NEW(m_vp)->due - _ST_LAST_CLOCK_NEW(m_vp));
        timeout = (int) (min_timeout / 1000);
    }

    if (m_st_epoll_data->pid != getpid()) {
        /* We probably forked, reinitialize epoll set */
        close(m_st_epoll_data->epfd);
        m_st_epoll_data->epfd = epoll_create(m_st_epoll_data->fd_hint);
        if (m_st_epoll_data->epfd < 0) {
            /* There is nothing we can do here, will retry later */
            return;
        }
        fcntl(m_st_epoll_data->epfd, F_SETFD, FD_CLOEXEC);
        m_st_epoll_data->pid = getpid();
		printf("aaaaaaaaaaaaaaaaaaaaaa*************");
        /* Put all descriptors on ioq into new epoll set */
        memset(m_st_epoll_data->fd_data, 0,
               m_st_epoll_data->fd_data_size * sizeof(_epoll_fd_data_t));
		printf("aaaaaaaaaaaaaaaaaaaaaa2*************");
        m_st_epoll_data->evtlist_cnt = 0;
        for (q = _ST_IOQ_NEW(m_vp).next; q != &_ST_IOQ_NEW(m_vp); q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            st_epoll_pollset_add(pq->pds, pq->npds);
        }
		printf("aaaaaaaaaaaaaaaaaaaaaa3*************");
    }

    /* Check for I/O operations */
    nfd = epoll_wait(m_st_epoll_data->epfd, m_st_epoll_data->evtlist,
                     m_st_epoll_data->evtlist_size, timeout);

    if (nfd > 0) {
        for (i = 0; i < nfd; i++) {
            osfd = m_st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS_NEW(osfd) = m_st_epoll_data->evtlist[i].events;
            if (_ST_EPOLL_REVENTS_NEW(osfd) & (EPOLLERR | EPOLLHUP)) {
                /* Also set I/O bits on error */
                _ST_EPOLL_REVENTS_NEW(osfd) |= _ST_EPOLL_EVENTS_NEW(osfd);
            }
        }

        for (q = _ST_IOQ_NEW(m_vp).next; q != &_ST_IOQ_NEW(m_vp); q = q->next) {
            pq = _ST_POLLQUEUE_PTR(q);
            notify = 0;
            epds = pq->pds + pq->npds;

            for (pds = pq->pds; pds < epds; pds++) {
                if (_ST_EPOLL_REVENTS_NEW(pds->fd) == 0) {
                    pds->revents = 0;
                    continue;
                }
                osfd = pds->fd;
                events = pds->events;
                revents = 0;
                if ((events & POLLIN) && (_ST_EPOLL_REVENTS_NEW(osfd) & EPOLLIN))
                    revents |= POLLIN;
                if ((events & POLLOUT) && (_ST_EPOLL_REVENTS_NEW(osfd) & EPOLLOUT))
                    revents |= POLLOUT;
                if ((events & POLLPRI) && (_ST_EPOLL_REVENTS_NEW(osfd) & EPOLLPRI))
                    revents |= POLLPRI;
                if (_ST_EPOLL_REVENTS_NEW(osfd) & EPOLLERR)
                    revents |= POLLERR;
                if (_ST_EPOLL_REVENTS_NEW(osfd) & EPOLLHUP)
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
                    _ST_DEL_SLEEPQ_NEW(m_vp, pq->thread);
                pq->thread->state = _ST_ST_RUNNABLE;
                _ST_ADD_RUNQ_NEW(m_vp, pq->thread);
            }
        }

        for (i = 0; i < nfd; i++) {
            /* Delete/modify descriptors that fired */
            osfd = m_st_epoll_data->evtlist[i].data.fd;
            _ST_EPOLL_REVENTS_NEW(osfd) = 0;
            events = _ST_EPOLL_EVENTS_NEW(osfd);
            op = events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            ev.events = events;
            ev.data.fd = osfd;
            if (epoll_ctl(m_st_epoll_data->epfd, op, osfd, &ev) == 0 &&
                op == EPOLL_CTL_DEL) {
                m_st_epoll_data->evtlist_cnt--;
            }
        }
    }
}

int CStEvent::st_epoll_fd_new(int osfd)
{
    if (osfd >= m_st_epoll_data->fd_data_size &&
        st_epoll_fd_data_expand(osfd) < 0)
        return -1;

    return 0;   
}

int CStEvent::st_epoll_fd_close(int osfd)
{
    if (_ST_EPOLL_READ_CNT_NEW(osfd) || _ST_EPOLL_WRITE_CNT_NEW(osfd) ||
        _ST_EPOLL_EXCEP_CNT_NEW(osfd)) {
        errno = EBUSY;
        return -1;
    }

    return 0;
}

int CStEvent::st_epoll_fd_getlimit(void)
{
    /* zero means no specific limit */
    return 0;
}

int CStEvent::st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}






