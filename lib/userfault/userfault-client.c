#include "userfault-client.h"
#include <dbg.h>
#include "common.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/syscall.h> /* for __NR_userfaultfd */
#include <unistd.h>      /* for syscall */
#include <linux/un.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/user.h> /* for PAGE_SIZE */

#define MAX_PENDING 100

/* for sending unix domain sockets */
/* http://www.thomasstover.com/uds.html */

int send_fd(int socket, int fd_to_send) {
  struct msghdr socket_message;
  struct iovec io_vector[1];
  struct cmsghdr *control_message = NULL;
  char message_buffer[sizeof(pid_t)];
  /* storage space needed for an ancillary element with a paylod of length is CMSG_SPACE(sizeof(length)) */
  char ancillary_element_buffer[CMSG_SPACE(sizeof(int))];
  int available_ancillary_element_buffer_space;

  /* at least one vector of one byte must be sent */
  *((pid_t*) (&message_buffer[0])) = getpid();
  io_vector[0].iov_base = message_buffer;
  io_vector[0].iov_len = sizeof(message_buffer);

  /* initialize socket message */
  memset(&socket_message, 0, sizeof(struct msghdr));
  socket_message.msg_iov = io_vector;
  socket_message.msg_iovlen = 1;

  /* provide space for the ancillary data */
  available_ancillary_element_buffer_space = sizeof(ancillary_element_buffer);
  memset(ancillary_element_buffer, 0, available_ancillary_element_buffer_space);
  socket_message.msg_control = ancillary_element_buffer;
  socket_message.msg_controllen = available_ancillary_element_buffer_space;

  /* initialize a single ancillary data element for fd passing */
  control_message = CMSG_FIRSTHDR(&socket_message);
  control_message->cmsg_level = SOL_SOCKET;
  control_message->cmsg_type = SCM_RIGHTS;
  control_message->cmsg_len = CMSG_LEN(sizeof(int));
  *((int *) CMSG_DATA(control_message)) = fd_to_send;

  return sendmsg(socket, &socket_message, 0);
}

int connect_monitor(char * socket_path) {
  int socket_fd;
  struct sockaddr_un address;
  socklen_t sockaddr_len = (socklen_t) sizeof(struct sockaddr_un);

  socket_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    printf("socket() failed\n");
    return -1;
  }

  /* clear the addresss structure */
  memset(&address, 0, sockaddr_len);

  address.sun_family = AF_LOCAL;
  snprintf(address.sun_path, UNIX_PATH_MAX, "%s", socket_path);

  if (connect(socket_fd, (struct sockaddr *) &address,
              sockaddr_len) != 0) {
    log_err("%s: connect() failed", __func__);
    return -1;
  }

  setnonblocking(socket_fd);
  return socket_fd;
}

/*
 * Function ufd_version_check from David Gilbert's
 * Qemu postcopy code postcopy-ram.c
 */
bool ufd_version_check(int ufd)
{
  struct uffdio_api api_struct;
  uint64_t ioctl_mask;

  api_struct.api = UFFD_API;
  api_struct.features = UFFD_FEATURE_EVENT_FORK |
                        UFFD_FEATURE_EVENT_REMAP |
                        UFFD_FEATURE_EVENT_REMOVE |
                        UFFD_FEATURE_EVENT_UNMAP;
  if (ioctl(ufd, UFFDIO_API, &api_struct)) {
      log_err("%s: UFFDIO_API failed", __func__);
      return false;
  }

  if (api_struct.api != UFFD_API) {
      log_err("%s: Result of looking up UFFDIO_API does not match: %Lu\n", __func__, api_struct.api);
      return false;
  }

  ioctl_mask = (__u64)1 << _UFFDIO_REGISTER |
               (__u64)1 << _UFFDIO_UNREGISTER;
  if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
      log_err("%s: missing userfault features: %lx", __func__,
              (uint64_t)(~api_struct.ioctls & ioctl_mask));
      return false;
  }

  return true;
}

int disable_ufd_area(int ufd, void * area, uint64_t size) {
  struct uffdio_range range_struct;

  range_struct.start = (uint64_t)(uintptr_t)area;
  range_struct.len = (uint64_t)size;
  if (ioctl(ufd, UFFDIO_UNREGISTER, &range_struct)) {
    log_err("%s: unregistering userfault region beginning at %lx for %lu bytes", __func__,
            (uint64_t)area, size);
    return -1;
  }
  return 0;
}

int ufd_syscall(void) {
  int ufd;

  ufd = syscall(__NR_userfaultfd, O_CLOEXEC|O_NONBLOCK);
  if (ufd == -1) {
    log_err("%s: ufd_syscall", __func__);
    return -1;
  }

  if (!ufd_version_check(ufd)) {
    return -1;
  }

  fcntl(ufd, F_SETFL, O_NONBLOCK);

  return ufd;
}

int enable_ufd_area(int * ufd, void * area, uint64_t size) {
  struct uffdio_register reg_struct;
  uint64_t feature_mask;

  if (madvise(area, size, MADV_DONTFORK)) {
    munmap(area, size);
    log_err("%s: failed madvise DONTFORK", __func__);
    return -1;
  }

  if (madvise(area, size, MADV_DONTNEED)) {
    munmap(area, size);
    log_err("%s: failed madvise DONTNEED", __func__);
    return -1;
  }

#ifdef MADV_NOHUGEPAGE
  if (madvise(area, size, MADV_NOHUGEPAGE)) {
    munmap(area, size);
    log_err("%s: failed madvise NOHUGEPAGE", __func__);
    return -1;
  }
#endif

  // create the ufd
  *ufd = ufd_syscall();
  if (*ufd == -1) {
    log_err("%s: userfaultfd not available", __func__);
    close(*ufd);
    return -1;
  }

  reg_struct.range.start = (uint64_t)(uintptr_t)area;
  reg_struct.range.len = size;
  reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

  if (ioctl(*ufd, UFFDIO_REGISTER, &reg_struct)) {
    log_err("%s: failed registering userfault region beginning at %lx for %lu bytes",
             __func__, (uint64_t)area, size);
    return -1;
  }
  else {
    log_info("%s: registered userfault region beginning at %lx for %lu bytes",
             __func__, (uint64_t)area, size);
  }

  feature_mask = (__u64)1 << _UFFDIO_WAKE |
                 (__u64)1 << _UFFDIO_COPY |
                 (__u64)1 << _UFFDIO_REMAP |
                 (__u64)1 << _UFFDIO_ZEROPAGE;
  if ((reg_struct.ioctls & feature_mask) != feature_mask) {
    log_err("%s: missing userfault map features: %lx", __func__,
            (uint64_t)(~reg_struct.ioctls & feature_mask));
    return -1;
  }

  return 0;
}

void * allocate_userfault(int *ufd, uint64_t size) {
  int socket_fd;
  void *ret = NULL;
  int rc;

  check(size % PAGE_SIZE == 0,
        "size of userfault region (%d bytes) is not aligned to page size (%d bytes)",
        size, PAGE_SIZE);

  /* allocate */
  if (posix_memalign(&ret, PAGE_SIZE, size)) {
    log_err("%s: failed to allocate region of %lx bytes", __func__, size);
    goto error;
  }
  else {
    log_info("%s: array allocated. Size: %lu bytes, Addr: %p - %p", __func__,
             size, ret, ret + size);
  }

  /* register region and get userfaultfd */
  rc = enable_ufd_area(ufd, ret, size);
  check((rc == 0) && ret, "failed to register ufd for %p",ret);

  /* connect to monitor */
  char socket_path[] = "/var/run/fluidmem/monitor.socket";
  socket_fd = connect_monitor(socket_path);
  check(socket_fd > 0, "failed connecting to monitor");

  /* send ufd to monitor */
  rc = send_fd(socket_fd,*ufd);
  check(rc >= 0, "sending ufd failed");
  close(socket_fd);

  return ret;

error:
  if (ret)
    disable_ufd_area(*ufd, ret, size);

  if (*ufd)
    close(*ufd);

  if (socket_fd)
    close(socket_fd);

  return NULL;
}
