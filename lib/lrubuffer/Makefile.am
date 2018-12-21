include_HEADERS = LRUBufferWrapper.h c_cache_node.h lrucache.h

LRUBUFFER_FLAGS =
if DEBUG
LRUBUFFER_FLAGS += -g -DDEBUG
else
LRUBUFFER_FLAGS += -DNDEBUG
if PROFILING
LRUBUFFER_FLAGS += -g
endif
endif
if TRACE
LRUBUFFER_FLAGS += -DTRACE
endif
if MONITORSTATS
LRUBUFFER_FLAGS += -DMONITORSTATS
endif
if LOCK_DEBUG
LRUBUFFER_FLAGS += -DLOCK_DEBUG
endif
if PAGECACHE
LRUBUFFER_FLAGS += -DPAGECACHE
endif
if TIMING
LRUBUFFER_FLAGS += -DTIMING
endif

noinst_LTLIBRARIES = libLRUBufferImpl.la

libLRUBufferImpl_la_LDFLAGS = -L../monitorstats -Wl,-rpath,../monitorstats
libLRUBufferImpl_la_LIBADD = ../monitorstats/libmonitorstats.la

libLRUBufferImpl_la_SOURCES = LRUBuffer.hh LRUBufferImpl.hh LRUBufferImpl.cc
libLRUBufferImpl_la_CPPFLAGS = -std=c++0x -I$(SCALEOS_ROOT)/include -I$(SCALEOS_ROOT)/lib/monitorstats $(LRUBUFFER_FLAGS)

lib_LTLIBRARIES = liblrubuffer.la
liblrubuffer_la_SOURCES = LRUBufferWrapper.h LRUBufferWrapper.cc
liblrubuffer_la_LIBADD = libLRUBufferImpl.la

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
if MONITORSTATS
AM_CPPFLAGS += -DMONITORSTATS
endif
if LOCK_DEBUG
AM_CPPFLAGS += -DLOCK_DEBUG
endif
if TIMING
AM_CPPFLAGS += -DTIMING
endif
if PAGECACHE
AM_CPPFLAGS += -DPAGECACHE
endif
