bin_PROGRAMS = monitor ui
include_HEADERS = pollfd_vector.h ui_processing.h

MONITOR_FLAGS =
if MONITORSTATS
MONITOR_FLAGS += -DMONITORSTATS
endif
if TIMING
MONITOR_FLAGS += -DTIMING
endif
if DEBUG
MONITOR_FLAGS += -g -DDEBUG
endif
if TRACE
MONITOR_FLAGS += -DTRACE
endif
if PAGECACHE
MONITOR_FLAGS += -DPAGECACHE
endif
if PAGECACHE_ZEROPAGE_OPTIMIZATION
MONITOR_FLAGS += -DPAGECACHE_ZEROPAGE_OPTIMIZATION
endif
if REAPERTHREAD
MONITOR_FLAGS += -DREAPERTHREAD
endif
if AFFINITY
MONITOR_FLAGS += -DAFFINITY
endif

monitor_SOURCES = monitor.c ui_processing.c pollfd_vector.c
monitor_CFLAGS = \
  $(MONITOR_FLAGS) \
  -I$(SCALEOS_ROOT)/lib/monitorstats \
  -I$(SCALEOS_ROOT)/lib/userfault \
  -I$(SCALEOS_ROOT)/include \
  -I$(SCALEOS_ROOT)/lib/externram \
  -I$(SCALEOS_ROOT)/lib/lrubuffer \
  -I$(SCALEOS_ROOT)/lib/pagecache

monitor_LDFLAGS = \
  -L$(SCALEOS_ROOT)/lib/monitorstats/.libs \
  -L$(SCALEOS_ROOT)/lib/userfault/.libs \
  -L$(SCALEOS_ROOT)/lib/externram/.libs \
  -L$(SCALEOS_ROOT)/lib/lrubuffer/.libs \
  -L$(SCALEOS_ROOT)/lib/pagecache/.libs \
  -L$(RAMCLOUD_ROOT)/obj.master \
  -Wl,-rpath,$(RAMCLOUD_ROOT)/obj.master,-rpath,$(SCALEOS_ROOT)/lib/monitorstats/.libs,-rpath,$(SCALEOS_ROOT)/lib/userfault/.libs,-rpath,$(SCALEOS_ROOT)/lib/externram/.libs,-rpath,$(SCALEOS_ROOT)/lib/lrubuffer/.libs,-rpath,$(SCALEOS_ROOT)/lib/pagecache/.libs

monitor_LDADD = \
  -lpthread \
  -lmonitorstats \
  -luserfault \
  -lexternram \
  -llrubuffer \
  -lpagecache


ui_SOURCES = ui.c
ui_CFLAGS = -I$(SCALEOS_ROOT)/include
if TIMING
ui_CPPFLAGS = -DTIMING
endif

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
if CCACHE
AM_CPPFLAGS += -DC_CACHE
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
if LOCK_DEBUG
AM_CPPFLAGS += -DLOCK_DEBUG
endif
if TIMING
AM_CPPFLAGS += -DTIMING
endif
if REAPERTHREAD
AM_CPPFLAGS += -DREAPERTHREAD
endif
if AFFINITY
AM_CPPFLAGS += -DAFFINITY
endif
