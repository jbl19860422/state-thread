#ifndef _ST_EVENT_H_
#define _ST_EVENT_H_
#include "event.h"
#include "st_vp.h"
#include "st_common.h"
class CStVp;
class CStEvent {
public:
	int 	st_epoll_init();
	int 	st_epoll_fd_data_expand(int maxfd);
	void 	st_epoll_evtlist_expand(void);
	void 	st_epoll_pollset_del(struct pollfd *pds, int npds);
	int		st_epoll_pollset_add(struct pollfd *pds, int npds);
	void 	st_epoll_dispatch(void);
	int 	st_epoll_fd_new(int osfd);
	int 	st_epoll_fd_close(int osfd);
	int 	st_epoll_is_supported(void);
	
public:
	int 	st_epoll_fd_getlimit(void);
public:
	struct 	_st_epolldata* m_st_epoll_data;
	CStVp* m_vp;
};

#endif
