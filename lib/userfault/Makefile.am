include_HEADERS = userfault.h common.h userfault-client.h

lib_LTLIBRARIES = libuserfault.la libuserfault_client.la
libuserfault_la_SOURCES = userfault.c userfault.h
libuserfault_la_CFLAGS = -I$(SCALEOS_ROOT)/include -I$(SCALEOS_ROOT)/lib/externram -I$(SCALEOS_ROOT)/lib/lrubuffer -I$(SCALEOS_ROOT)/lib/pagecache -I$(SCALEOS_ROOT)/lib/monitorstats
libuserfault_la_LDFLAGS = \
  -L$(SCALEOS_ROOT)/lib/externram/.libs \
  -L$(SCALEOS_ROOT)/lib/lrubuffer/.libs \
  -L$(SCALEOS_ROOT)/lib/pagecache/.libs \
  -L$(SCALEOS_ROOT)/lib/monitorstats/.libs \
  -Wl,-rpath,$(SCALEOS_ROOT)/lib/externram/.libs,-rpath,$(SCALEOS_ROOT)/lib/lrubuffer/.libs,-rpath,$(SCALEOS_ROOT)/lib/pagecache/.libs,-rpath,$(SCALEOS_ROOT)/lib/monitorstats/.libs
libuserfault_la_LIBADD = \
  -lzookeeper_mt \
  -lexternram \
  -llrubuffer \
  -lpagecache \
  -lmonitorstats

libuserfault_client_la_SOURCES = userfault-client.c userfault-client.h
libuserfault_client_la_CFLAGS = -I../../include

AM_CPPFLAGS =
if DEBUG
AM_CPPFLAGS += -g -DDEBUG
else
AM_CPPFLAGS += -DNDEBUG
if PROFILING
AM_CPPFLAGS += -g
endif
endif
if TRACE
AM_CPPFLAGS += -DTRACE
endif
if ! CCACHE
AM_CPPFLAGS += -DCACHE
endif
if PAGECACHE
AM_CPPFLAGS += -DPAGECACHE
endif
if MONITORSTATS
AM_CPPFLAGS += -DMONITORSTATS
endif
if THREADED_WRITE_TO_EXTERNRAM
AM_CPPFLAGS += -DTHREADED_WRITE_TO_EXTERNRAM
endif
if THREADED_PREFETCH
AM_CPPFLAGS += -DTHREADED_PREFETCH
endif
if THREADED_REINIT
AM_CPPFLAGS += -DTHREADED_REINIT
endif
if PAGECACHE_ZEROPAGE_OPTIMIZATION
AM_CPPFLAGS += -DPAGECACHE_ZEROPAGE_OPTIMIZATION
endif
if LOCK_DEBUG
AM_CPPFLAGS += -DLOCK_DEBUG
endif
if TIMING
AM_CPPFLAGS += -DTIMING
endif
if ENABLE_AFFINITY
AM_CPPFLAGS += -DENABLE_AFFINITY
endif
