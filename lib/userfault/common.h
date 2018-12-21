/*
 * Copyright 2016 Blake Caldwell, University of Colorado,  All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Blake Caldwell <blake.caldwell@colorado.edu>, January 2016
 */

#ifndef USERFAULT_COMMON_H
#define USERFAULT_COMMON_H

#include <fcntl.h>

int setnonblocking(int fd) {
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
    flags = 0;

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#endif
