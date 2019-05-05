#include "st_io.h"
#include "stdio.h"

CStIO::CStIO()
{
	m_st_netfd_freelist = NULL;
}
int CStIO::st_io_init(void)
{
  struct sigaction sigact;
  struct rlimit rlim;
  int fdlim;
  m_st_netfd_freelist = NULL;
  
  /* Ignore SIGPIPE */
  sigact.sa_handler = SIG_IGN;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  if (sigaction(SIGPIPE, &sigact, NULL) < 0)
    return -1;

  /* Set maximum number of open file descriptors */
  if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;

  m_event = new CStEvent;
  m_event->m_vp = m_vp;
  fdlim = m_event->st_epoll_fd_getlimit();
  if (fdlim > 0 && rlim.rlim_max > (rlim_t) fdlim) {
    rlim.rlim_max = fdlim;
  }

  /* when rlimit max is negative, for example, osx, use cur directly. */
  /* @see https://github.com/winlinvip/simple-rtmp-server/issues/336 */
  if ((int)rlim.rlim_max < 0) {
    m_st_osfd_limit = (int)(fdlim > 0? fdlim : rlim.rlim_cur);
    return 0;
  }

  rlim.rlim_cur = rlim.rlim_max;
  if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;
  m_st_osfd_limit = (int) rlim.rlim_max;

  if(0 != m_event->st_epoll_init())
  	return -1;
  
  return 0;
}

int CStIO::st_netfd_fileno(_st_netfd_t *fd)
{
  return (fd->osfd);
}

int CStIO::st_getfdlimit(void)
{
  return m_st_osfd_limit;
}

void CStIO::st_netfd_free(_st_netfd_t *fd)
{
  printf("\st_netfd_free 1.o size=%d,m_st_netfd_freelist=%d\n",m_event->m_st_epoll_data->fd_data_size,m_st_netfd_freelist);

  if (!fd->inuse)
    return;

  fd->inuse = 0;
  if (fd->aux_data)
    st_netfd_free_aux_data(fd);
  if (fd->private_data && fd->destructor)
    (*(fd->destructor))(fd->private_data);
  fd->private_data = NULL;
  fd->destructor = NULL;
  fd->next = m_st_netfd_freelist;//挂到free队列
  m_st_netfd_freelist = fd;
}

void CStIO::st_netfd_free_aux_data(_st_netfd_t *fd)
{
  fd->aux_data = NULL;
}

_st_netfd_t* CStIO::st_netfd_new(int osfd, int nonblock, int is_socket)
{
  _st_netfd_t *fd;
  int flags = 1;
  printf("\nst_netfd_new 1.o size=%d\n",m_event->m_st_epoll_data->fd_data_size);
  if (m_event->st_epoll_fd_new(osfd) < 0)//事件系统通知
  {
  	printf("\nst_netfd_new 1.1\n");
  	return NULL;
  }
  printf("\nst_netfd_new 2 %d\n",m_event->m_st_epoll_data->fd_data_size);
  if (m_st_netfd_freelist) {//从空闲队列取出一个
    fd = m_st_netfd_freelist;
	printf("\nst_netfd_new 3.00 %d\n",m_event->m_st_epoll_data->fd_data_size);
    m_st_netfd_freelist = m_st_netfd_freelist->next;
  } else {
    fd = (_st_netfd_t*)calloc(1, sizeof(_st_netfd_t));//否则创建一个
    printf("\nst_netfd_new 3.0 %d\n",m_event->m_st_epoll_data->fd_data_size);
    if (!fd)
      return NULL;
  }
  printf("\nst_netfd_new 3 %d\n",m_event->m_st_epoll_data->fd_data_size);
  fd->osfd = osfd;
  printf("\nst_netfd_new 31 %d\n",m_event->m_st_epoll_data->fd_data_size);
  fd->inuse = 1;
  printf("\nst_netfd_new 32 %d\n",m_event->m_st_epoll_data->fd_data_size);
  fd->next = NULL;
  printf("\nst_netfd_new 4 %d\n",m_event->m_st_epoll_data->fd_data_size);
  if (nonblock) {
    /* Use just one system call */
    if (is_socket && ioctl(osfd, FIONBIO, &flags) != -1)
      return fd;
    /* Do it the Posix way */
    if ((flags = fcntl(osfd, F_GETFL, 0)) < 0 ||
		fcntl(osfd, F_SETFL, flags | O_NONBLOCK) < 0) {
      	st_netfd_free(fd);
      return NULL;
    }
  }
  printf("\nst_netfd_new 5 %d\n",m_event->m_st_epoll_data->fd_data_size);
  return fd;
}

_st_netfd_t *CStIO::st_netfd_open(int osfd)
{
  return st_netfd_new(osfd, 1, 0);
}

_st_netfd_t *CStIO::st_netfd_open_socket(int osfd)
{
  return st_netfd_new(osfd, 1, 1);
}

int CStIO::st_netfd_close(_st_netfd_t *fd)
{
  if (m_event->st_epoll_fd_close(fd->osfd) < 0)
    return -1;

  st_netfd_free(fd);
  return close(fd->osfd);
}

int CStIO::st_netfd_poll(_st_netfd_t *fd, int how, st_utime_t timeout)
{
  struct pollfd pd;
  int n;

  pd.fd = fd->osfd;
  pd.events = (short) how;
  pd.revents = 0;

  if ((n = m_vp->st_poll(&pd, 1, timeout)) < 0)
    return -1;//comment
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

_st_netfd_t *CStIO::st_accept(_st_netfd_t *fd, struct sockaddr *addr, int *addrlen,
		       st_utime_t timeout)
{
  int osfd, err;
  _st_netfd_t *newfd;

  while ((osfd = accept(fd->osfd, addr, (socklen_t *)addrlen)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return NULL;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return NULL;
  }

  /* On some platforms the new socket created by accept() inherits */
  /* the nonblocking attribute of the listening socket */
#if defined (MD_ACCEPT_NB_INHERITED)
  newfd = st_netfd_new(osfd, 0, 1);
#elif defined (MD_ACCEPT_NB_NOT_INHERITED)
  newfd = st_netfd_new(osfd, 1, 1);
#else
#error Unknown OS
#endif

  if (!newfd) {
    err = errno;
    close(osfd);
    errno = err;
  }

  return newfd;
}

int CStIO::st_connect(_st_netfd_t *fd, const struct sockaddr *addr, int addrlen,
		  st_utime_t timeout)
{
 int n, err = 0;

 while (connect(fd->osfd, addr, addrlen) < 0) {
   if (errno != EINTR) {
	 /*
	  * On some platforms, if connect() is interrupted (errno == EINTR)
	  * after the kernel binds the socket, a subsequent connect()
	  * attempt will fail with errno == EADDRINUSE.  Ignore EADDRINUSE
	  * iff connect() was previously interrupted.  See Rich Stevens'
	  * "UNIX Network Programming," Vol. 1, 2nd edition, p. 413
	  * ("Interrupted connect").
	  */
	 if (errno != EINPROGRESS && (errno != EADDRINUSE || err == 0))
   		return -1;
	 /* Wait until the socket becomes writable */
	 if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
   		return -1;
	 /* Try to find out whether the connection setup succeeded or failed */
	 n = sizeof(int);
	 if (getsockopt(fd->osfd, SOL_SOCKET, SO_ERROR, (char *)&err,
			(socklen_t *)&n) < 0)
  		 return -1;
	 if (err) {
	   errno = err;
	   return -1;
	 }
	 break;
   }
   err = 1;
 }

 return 0;
}

  ssize_t CStIO::st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout)
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
  
  
  int CStIO::st_read_resid(_st_netfd_t *fd, void *buf, size_t *resid,
			st_utime_t timeout)
  {
	struct iovec iov, *riov;
	int riov_size, rv;
  
	iov.iov_base = buf;
	iov.iov_len = *resid;
	riov = &iov;
	riov_size = 1;
	rv = st_readv_resid(fd, &riov, &riov_size, timeout);
	*resid = iov.iov_len;
	return rv;
  }
  
  
  ssize_t CStIO::st_readv(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
		   st_utime_t timeout)
  {
	ssize_t n;
  
	while ((n = readv(fd->osfd, iov, iov_size)) < 0) {
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
  
  int CStIO::st_readv_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
			 st_utime_t timeout)
  {
	ssize_t n;
  
	while (*iov_size > 0) {
	  if (*iov_size == 1)
		n = read(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
	  else
		n = readv(fd->osfd, *iov, *iov_size);
	  if (n < 0) {
		if (errno == EINTR)
	  continue;
		if (!_IO_NOT_READY_ERROR)
	  return -1;
	  } else if (n == 0)
		break;
	  else {
		while ((size_t) n >= (*iov)->iov_len) {
	  n -= (*iov)->iov_len;
	  (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
	  (*iov)->iov_len = 0;
	  (*iov)++;
	  (*iov_size)--;
	  if (n == 0)
		break;
		}
		if (*iov_size == 0)
	  break;
		(*iov)->iov_base = (char *) (*iov)->iov_base + n;
		(*iov)->iov_len -= n;
	  }
	  /* Wait until the socket becomes readable */
	  if (st_netfd_poll(fd, POLLIN, timeout) < 0)
		return -1;
	}
  
	return 0;
  }
  
  
  ssize_t CStIO::st_read_fully(_st_netfd_t *fd, void *buf, size_t nbyte,
				st_utime_t timeout)
  {
	size_t resid = nbyte;
	return st_read_resid(fd, buf, &resid, timeout) == 0 ?
	  (ssize_t) (nbyte - resid) : -1;
  }
  
  
  int CStIO::st_write_resid(_st_netfd_t *fd, const void *buf, size_t *resid,
			 st_utime_t timeout)
  {
	struct iovec iov, *riov;
	int riov_size, rv;
  
	iov.iov_base = (void *) buf;	  /* we promise not to modify buf */
	iov.iov_len = *resid;
	riov = &iov;
	riov_size = 1;
	rv = st_writev_resid(fd, &riov, &riov_size, timeout);
	*resid = iov.iov_len;
	return rv;
  }
  
  
  ssize_t CStIO::st_write(_st_netfd_t *fd, const void *buf, size_t nbyte,
		   st_utime_t timeout)
  {
	size_t resid = nbyte;
	return st_write_resid(fd, buf, &resid, timeout) == 0 ?
	  (ssize_t) (nbyte - resid) : -1;
  }
  
  
  ssize_t CStIO::st_writev(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
			st_utime_t timeout)
  {
	ssize_t n, rv;
	size_t nleft, nbyte;
	int index, iov_cnt;
	struct iovec *tmp_iov;
	struct iovec local_iov[_LOCAL_MAXIOV];
  
	/* Calculate the total number of bytes to be sent */
	nbyte = 0;
	for (index = 0; index < iov_size; index++)
	  nbyte += iov[index].iov_len;
  
	rv = (ssize_t)nbyte;
	nleft = nbyte;
	tmp_iov = (struct iovec *) iov;   /* we promise not to modify iov */
	iov_cnt = iov_size;
  
	while (nleft > 0) {
	  if (iov_cnt == 1) {
		if (st_write(fd, tmp_iov[0].iov_base, nleft, timeout) != (ssize_t) nleft)
	  rv = -1;
		break;
	  }
	  if ((n = writev(fd->osfd, tmp_iov, iov_cnt)) < 0) {
		if (errno == EINTR)
	  continue;
		if (!_IO_NOT_READY_ERROR) {
	  rv = -1;
	  break;
		}
	  } else {
		if ((size_t) n == nleft)
	  break;
		nleft -= n;
		/* Find the next unwritten vector */
		n = (ssize_t)(nbyte - nleft);
		for (index = 0; (size_t) n >= iov[index].iov_len; index++)
	  n -= iov[index].iov_len;
  
		if (tmp_iov == iov) {
	  /* Must copy iov's around */
	  if (iov_size - index <= _LOCAL_MAXIOV) {
		tmp_iov = local_iov;
	  } else {
		tmp_iov = (struct iovec*)calloc(1, (iov_size - index) * sizeof(struct iovec));
		if (tmp_iov == NULL)
		  return -1;
	  }
		}
  
		/* Fill in the first partial read */
		tmp_iov[0].iov_base = &(((char *)iov[index].iov_base)[n]);
		tmp_iov[0].iov_len = iov[index].iov_len - n;
		index++;
		/* Copy the remaining vectors */
		for (iov_cnt = 1; index < iov_size; iov_cnt++, index++) {
	  tmp_iov[iov_cnt].iov_base = iov[index].iov_base;
	  tmp_iov[iov_cnt].iov_len = iov[index].iov_len;
		}
	  }
	  /* Wait until the socket becomes writable */
	  if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
		rv = -1;
		break;
	  }
	}
  
	if (tmp_iov != iov && tmp_iov != local_iov)
	  free(tmp_iov);
  
	return rv;
  }
  
  
  int CStIO::st_writev_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
			  st_utime_t timeout)
  {
	ssize_t n;
  
	while (*iov_size > 0) {
	  if (*iov_size == 1)
		n = write(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
	  else
		n = writev(fd->osfd, *iov, *iov_size);
	  if (n < 0) {
		if (errno == EINTR)
	  continue;
		if (!_IO_NOT_READY_ERROR)
	  return -1;
	  } else {
		while ((size_t) n >= (*iov)->iov_len) {
	  n -= (*iov)->iov_len;
	  (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
	  (*iov)->iov_len = 0;
	  (*iov)++;
	  (*iov_size)--;
	  if (n == 0)
		break;
		}
		if (*iov_size == 0)
	  break;
		(*iov)->iov_base = (char *) (*iov)->iov_base + n;
		(*iov)->iov_len -= n;
	  }
	  /* Wait until the socket becomes writable */
	  if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
		return -1;
	}
  
	return 0;
  }

/*
* Simple I/O functions for UDP.
*/
int CStIO::st_recvfrom(_st_netfd_t *fd, void *buf, int len, struct sockaddr *from,
	  int *fromlen, st_utime_t timeout)
{
	int n;

	while ((n = recvfrom(fd->osfd, buf, len, 0, from, (socklen_t *)fromlen))< 0) {
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

  
int CStIO::st_sendto(_st_netfd_t *fd, const void *msg, int len,
		const struct sockaddr *to, int tolen, st_utime_t timeout)
{
	int n;

	while ((n = sendto(fd->osfd, msg, len, 0, to, tolen)) < 0) {
  		if (errno == EINTR)
			continue;
  		if (!_IO_NOT_READY_ERROR)
			return -1;
  		/* Wait until the socket becomes writable */
  		if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
			return -1;
	}

	return n;
}
  
  
int CStIO::st_recvmsg(_st_netfd_t *fd, struct msghdr *msg, int flags,
		 st_utime_t timeout)
{
	int n;

	while ((n = recvmsg(fd->osfd, msg, flags)) < 0) {
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
  
  
int CStIO::st_sendmsg(_st_netfd_t *fd, const struct msghdr *msg, int flags,
		 st_utime_t timeout)
{
	int n;

	while ((n = sendmsg(fd->osfd, msg, flags)) < 0) {
  		if (errno == EINTR)
			continue;
 		if (!_IO_NOT_READY_ERROR)
			return -1;
  		/* Wait until the socket becomes writable */
  		if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
			return -1;
	}
	return n;
}

  
  
/*
* To open FIFOs or other special files.
*/
_st_netfd_t *CStIO::st_open(const char *path, int oflags, mode_t mode)
{
	int osfd, err;
	_st_netfd_t *newfd;

	while ((osfd = open(path, oflags | O_NONBLOCK, mode)) < 0) {
  		if (errno != EINTR)
			return NULL;
	}

	newfd = st_netfd_new(osfd, 0, 0);
	if (!newfd) {
  		err = errno;
  		close(osfd);
  		errno = err;
	}

	return newfd;
}






