#ifndef _ST_IO_H_
#define _ST_IO_H_
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include "common.h"
#include "io.h"

#include "st_event.h"
#include "st_vp.h"
class CStVp;
class CStEvent;
class CStIO {
public:
	CStIO();
	int 		st_io_init(void);
	int 		st_getfdlimit(void);
	_st_netfd_t *st_netfd_new(int osfd, int nonblock, int is_socket);
	void 		st_netfd_free(_st_netfd_t *fd);
	void 		st_netfd_free_aux_data(_st_netfd_t *fd);
	_st_netfd_t *st_netfd_open(int osfd);
	
	_st_netfd_t *st_netfd_open_socket(int osfd);
	int 		st_netfd_close(_st_netfd_t *fd);
	int 		st_netfd_poll(_st_netfd_t *fd, int how, st_utime_t timeout);
	_st_netfd_t *st_accept(_st_netfd_t *fd, struct sockaddr *addr, int *addrlen,
		       st_utime_t timeout);
	int st_connect(_st_netfd_t *fd, const struct sockaddr *addr, int addrlen,
	       st_utime_t timeout);
	ssize_t st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout);

	int st_read_resid(_st_netfd_t *fd, void *buf, size_t *resid,
		  st_utime_t timeout);

	ssize_t st_readv(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
		 st_utime_t timeout);

	int st_readv_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
	   st_utime_t timeout);

    ssize_t st_read_fully(_st_netfd_t *fd, void *buf, size_t nbyte,
	      st_utime_t timeout);

	int st_write_resid(_st_netfd_t *fd, const void *buf, size_t *resid,
		   st_utime_t timeout);

	ssize_t st_write(_st_netfd_t *fd, const void *buf, size_t nbyte,
		 st_utime_t timeout);

	ssize_t st_writev(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
		  st_utime_t timeout);

	int st_writev_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
		    st_utime_t timeout);

	int st_recvfrom(_st_netfd_t *fd, void *buf, int len, struct sockaddr *from,
	  int *fromlen, st_utime_t timeout);

	int st_sendto(_st_netfd_t *fd, const void *msg, int len,
		const struct sockaddr *to, int tolen, st_utime_t timeout);

	int st_recvmsg(_st_netfd_t *fd, struct msghdr *msg, int flags,
		 st_utime_t timeout);

	int st_sendmsg(_st_netfd_t *fd, const struct msghdr *msg, int flags,
		 st_utime_t timeout);

	int 		st_netfd_fileno(_st_netfd_t *fd);
	_st_netfd_t *st_open(const char *path, int oflags, mode_t mode);
public:
	_st_netfd_t *m_st_netfd_freelist;
	int 		m_st_osfd_limit;
	CStEvent	*m_event;
	CStVp		*m_vp;
};

#endif
