bin_PROGRAMS = test test-nofluidmem test_readahead test_cases test_externram
dist_bin_SCRIPTS = test_readahead.sh test_cases.sh test_common.sh

test_SOURCES = test.c
test_CFLAGS = -I$(SCALEOS_ROOT)/lib/userfault -I$(SCALEOS_ROOT)/include
test_LDADD = -luserfault_client
test_LDFLAGS = -L$(SCALEOS_ROOT)/lib/userfault/.libs -Wl,-rpath,$(SCALEOS_ROOT)/lib/userfault/.libs

test_nofluidmem_SOURCES = test_nofluidmem.c
test_nofluidmem_CFLAGS = -I$(SCALEOS_ROOT)/include   -I$(SCALEOS_ROOT)/lib/monitorstats
test_nofluidmem_LDFLAGS = $(test_LDFLAGS) -L$(SCALEOS_ROOT)/lib/monitorstats/.libs -Wl,-rpath,$(SCALEOS_ROOT)/lib/monitorstats/.libs
test_nofluidmem_LDADD = $(test_LDADD) -lmonitorstats

test_readahead_SOURCES = test_readahead.c
test_readahead_CFLAGS = $(test_CFLAGS)
test_readahead_LDFLAGS = $(test_LDFLAGS)
test_readahead_LDADD = $(test_LDADD)

test_cases_SOURCES = test_cases.c
test_cases_CFLAGS = $(test_CFLAGS)
test_cases_LDFLAGS = $(test_LDFLAGS)
test_cases_LDADD = $(test_LDADD) -lpthread

test_externram_SOURCES = test_externram.c
test_externram_CFLAGS = $(test_CFLAGS) -I$(SCALEOS_ROOT)/lib/externram -I$(SCALEOS_ROOT)/lib/monitorstats
test_externram_LDFLAGS = $(test_LDFLAGS) -L$(SCALEOS_ROOT)/lib/externram/.libs -L$(SCALEOS_ROOT)/lib/monitorstats/.libs -Wl,-rpath,$(SCALEOS_ROOT)/lib/externram/.libs,-rpath,$(SCALEOS_ROOT)/lib/monitorstats/.libs
test_externram_LDADD = $(test_LDADD) -lexternram -lmonitorstats

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
if LOCK_DEBUG
AM_CPPFLAGS += -DLOCK_DEBUG
endif
if TIMING
AM_CPPFLAGS += -DTIMING
endif
if MONITORSTATS
AM_CPPFLAGS += -DMONITORSTATS
endif
