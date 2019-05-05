#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "common.h"

#ifdef MD_HAVE_KQUEUE
#include <sys/event.h>
#endif
#ifdef MD_HAVE_EPOLL
#include <sys/epoll.h>
#endif

#if defined(USE_POLL) && !defined(MD_HAVE_POLL)
/* Force poll usage if explicitly asked for it */
#define MD_HAVE_POLL
#endif


static struct _st_seldata {
    fd_set fd_read_set, fd_write_set, fd_exception_set;
    int fd_ref_cnts[FD_SETSIZE][3];
    int maxfd;
} *_st_select_data;

#define _ST_SELECT_MAX_OSFD      (_st_select_data->maxfd)
#define _ST_SELECT_READ_SET      (_st_select_data->fd_read_set)
#define _ST_SELECT_WRITE_SET     (_st_select_data->fd_write_set)
#define _ST_SELECT_EXCEP_SET     (_st_select_data->fd_exception_set)
#define _ST_SELECT_READ_CNT(fd)  (_st_select_data->fd_ref_cnts[fd][0])
#define _ST_SELECT_WRITE_CNT(fd) (_st_select_data->fd_ref_cnts[fd][1])
#define _ST_SELECT_EXCEP_CNT(fd) (_st_select_data->fd_ref_cnts[fd][2])


#ifdef MD_HAVE_POLL
static struct _st_polldata {
    struct pollfd *pollfds;
    int pollfds_size;
    int fdcnt;
} *_st_poll_data;

#define _ST_POLL_OSFD_CNT        (_st_poll_data->fdcnt) 
#define _ST_POLLFDS              (_st_poll_data->pollfds) 
#define _ST_POLLFDS_SIZE         (_st_poll_data->pollfds_size)
#endif  /* MD_HAVE_POLL */


#ifdef MD_HAVE_KQUEUE
typedef struct _kq_fd_data {
    int rd_ref_cnt;
    int wr_ref_cnt;
    int revents;
} _kq_fd_data_t;

static struct _st_kqdata {
    _kq_fd_data_t *fd_data;
    struct kevent *evtlist;
    struct kevent *addlist;
    struct kevent *dellist;
    int fd_data_size;
    int evtlist_size;
    int addlist_size;
    int addlist_cnt;
    int dellist_size;
    int dellist_cnt;
    int kq;
    pid_t pid;
} *_st_kq_data;

#ifndef ST_KQ_MIN_EVTLIST_SIZE
#define ST_KQ_MIN_EVTLIST_SIZE 64
#endif

#define _ST_KQ_READ_CNT(fd)      (_st_kq_data->fd_data[fd].rd_ref_cnt)
#define _ST_KQ_WRITE_CNT(fd)     (_st_kq_data->fd_data[fd].wr_ref_cnt)
#define _ST_KQ_REVENTS(fd)       (_st_kq_data->fd_data[fd].revents)
#endif  /* MD_HAVE_KQUEUE */


#ifdef MD_HAVE_EPOLL
typedef struct _epoll_fd_data {
    int rd_ref_cnt;
    int wr_ref_cnt;
    int ex_ref_cnt;
    int revents;
} _epoll_fd_data_t;

static struct _st_epolldata {
    _epoll_fd_data_t *fd_data;
    struct epoll_event *evtlist;
    int fd_data_size;
    int evtlist_size;
    int evtlist_cnt;
    int fd_hint;
    int epfd;
    pid_t pid;
} *_st_epoll_data;

#ifndef ST_EPOLL_EVTLIST_SIZE
/* Not a limit, just a hint */
#define ST_EPOLL_EVTLIST_SIZE 4096
#endif

#define _ST_EPOLL_READ_CNT(fd)   (_st_epoll_data->fd_data[fd].rd_ref_cnt)
#define _ST_EPOLL_WRITE_CNT(fd)  (_st_epoll_data->fd_data[fd].wr_ref_cnt)
#define _ST_EPOLL_EXCEP_CNT(fd)  (_st_epoll_data->fd_data[fd].ex_ref_cnt)
#define _ST_EPOLL_REVENTS(fd)    (_st_epoll_data->fd_data[fd].revents)

#define _ST_EPOLL_READ_BIT(fd)   (_ST_EPOLL_READ_CNT(fd) ? EPOLLIN : 0)
#define _ST_EPOLL_WRITE_BIT(fd)  (_ST_EPOLL_WRITE_CNT(fd) ? EPOLLOUT : 0)
#define _ST_EPOLL_EXCEP_BIT(fd)  (_ST_EPOLL_EXCEP_CNT(fd) ? EPOLLPRI : 0)
#define _ST_EPOLL_EVENTS(fd) \
    (_ST_EPOLL_READ_BIT(fd)|_ST_EPOLL_WRITE_BIT(fd)|_ST_EPOLL_EXCEP_BIT(fd))


#define _ST_EPOLL_READ_CNT_NEW(fd)   (m_st_epoll_data->fd_data[fd].rd_ref_cnt)
#define _ST_EPOLL_WRITE_CNT_NEW(fd)  (m_st_epoll_data->fd_data[fd].wr_ref_cnt)
#define _ST_EPOLL_EXCEP_CNT_NEW(fd)  (m_st_epoll_data->fd_data[fd].ex_ref_cnt)
#define _ST_EPOLL_REVENTS_NEW(fd)    (m_st_epoll_data->fd_data[fd].revents)

#define _ST_EPOLL_READ_BIT_NEW(fd)   (_ST_EPOLL_READ_CNT_NEW(fd) ? EPOLLIN : 0)
#define _ST_EPOLL_WRITE_BIT_NEW(fd)  (_ST_EPOLL_WRITE_CNT_NEW(fd) ? EPOLLOUT : 0)
#define _ST_EPOLL_EXCEP_BIT_NEW(fd)  (_ST_EPOLL_EXCEP_CNT_NEW(fd) ? EPOLLPRI : 0)
#define _ST_EPOLL_EVENTS_NEW(fd) \
    (_ST_EPOLL_READ_BIT_NEW(fd)|_ST_EPOLL_WRITE_BIT_NEW(fd)|_ST_EPOLL_EXCEP_BIT_NEW(fd))

#endif  /* MD_HAVE_EPOLL */


#endif
