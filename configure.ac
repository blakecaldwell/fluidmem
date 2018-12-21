AC_INIT([FluidMem], [0.2.0])
${CFLAGS=""}
${CXXFLAGS=""}
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(monitor/monitor.c)
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

# capture the root build directory for reference
# later when modifying rpath
SCALEOS_ROOT=$(pwd)
AC_SUBST(SCALEOS_ROOT)

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [enable debugging, default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])
AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")

AC_ARG_ENABLE(profiling,
AS_HELP_STRING([--enable-profiling],
               [enable profiling, default: no]),
[case "${enableval}" in
             yes) profiling=true ;;
             no)  profiling=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profiling]) ;;
esac],
[profiling=false])
AM_CONDITIONAL(PROFILING, test x"$profiling" = x"true")

AC_ARG_ENABLE(lock_debug,
AS_HELP_STRING([--enable-lock-debug],
               [enable lock debugging, default: no]),
[case "${enableval}" in
             yes) lock_debug=true ;;
             no)  lock_debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-lock-debug]) ;;
esac],
[lock_debug=false])
AM_CONDITIONAL(LOCK_DEBUG, test x"$lock_debug" = x"true")

AC_ARG_ENABLE(trace,
AS_HELP_STRING([--enable-trace],
               [enable tracing, default: no]),
[case "${enableval}" in
             yes) trace=true ;;
             no)  trace=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-trace]) ;;
esac],
[trace=false])
AM_CONDITIONAL(TRACE, test x"$trace" = x"true")

AC_ARG_ENABLE(ccache,
AS_HELP_STRING([--enable-ccache],
               [enable tracing, default: no]),
[case "${enableval}" in
             yes) ccache=true ;;
             no)  ccache=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-ccache]) ;;
esac],
[ccache=false])
AM_CONDITIONAL(CCACHE, test x"$ccache" = x"true")

AC_ARG_ENABLE(pagecache,
AS_HELP_STRING([--enable-pagecache],
               [enable page cache, default: no]),
[case "${enableval}" in
             yes) pagecache=true ;;
             no)  pagecache=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-pagecache]) ;;
esac],
[pagecache=false])
AM_CONDITIONAL(PAGECACHE, test x"$pagecache" = x"true")

AC_ARG_ENABLE(ramcloud,
AS_HELP_STRING([--enable-ramcloud],
               [enable ramcloud store in libexternram, default: no]),
[case "${enableval}" in
             yes) ramcloud=true
                  RAMCLOUD_ROOT=${SCALEOS_ROOT}/../RAMCloud
                  AC_SUBST(RAMCLOUD_ROOT)
                  ;;
             no)  ramcloud=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-ramcloud]) ;;
esac],
[ramcloud=false])
AM_CONDITIONAL(RAMCLOUD, test x"$ramcloud" = x"true")

AC_ARG_ENABLE(noop,
AS_HELP_STRING([--enable-noop],
               [enable noop store in libexternram, default: no]),
[case "${enableval}" in
             yes) noop=true ;;
             no)  noop=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-noop]) ;;
esac],
[noop=false])
AM_CONDITIONAL(NOOP, test x"$noop" = x"true")

AC_ARG_ENABLE(memcached,
AS_HELP_STRING([--enable-memcached],
               [enable memcached store in libexternram, default: no]),
[case "${enableval}" in
             yes) memcached=true ;;
             no)  memcached=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-memcached]) ;;
esac],
[memcached=false])
AM_CONDITIONAL(MEMCACHED, test x"$memcached" = x"true")

AC_ARG_ENABLE(monitorstats,
AS_HELP_STRING([--enable-monitorstats],
               [enable monitorstats, default: yes]),
[case "${enableval}" in
             yes) monitorstats=true ;;
             no)  monitorstats=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-monitorstats]) ;;
esac],
[monitorstats=true])
AM_CONDITIONAL(MONITORSTATS, test x"$monitorstats" = x"true")

AC_ARG_ENABLE(pagecache_zeropageopt,
AS_HELP_STRING([--enable-pagecache-zeropageopt],
               [enable the all-zero page optimiation in page cache, default: no]),
[case "${enableval}" in
             yes) pagecache_zeropageopt=true ;;
             no)  pagecache_zeropageopt=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-pagecache-zeropageopt]) ;;
esac],
[pagecache_zeropageopt=false])
AM_CONDITIONAL(PAGECACHE_ZEROPAGE_OPTIMIZATION, test x"$pagecache_zeropageopt" = x"true")

AC_ARG_ENABLE(threadedwrite,
AS_HELP_STRING([--enable-threadedwrite],
               [enable the threaded write to extern RAM, default: no]),
[case "${enableval}" in
             yes) threadedwrite=true ;;
             no)  threadedwrite=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-threadedwrite]) ;;
esac],
[threadedwrite=false])
AM_CONDITIONAL(THREADED_WRITE_TO_EXTERNRAM, test x"$threadedwrite" = x"true")

AC_ARG_ENABLE(threadedprefetch,
AS_HELP_STRING([--enable-threadedprefetch],
               [enable the threaded prefetch, default: no]),
[case "${enableval}" in
             yes) threadedprefetch=true ;;
             no)  threadedprefetch=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-threadedprefetch]) ;;
esac],
[threadedprefetch=false])
AM_CONDITIONAL(THREADED_PREFETCH, test x"$threadedprefetch" = x"true")

AC_ARG_ENABLE(timing,
AS_HELP_STRING([--enable-timing],
               [enable timing, default: no]),
[case "${enableval}" in
             yes) timing=true ;;
             no)  timing=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-timing]) ;;
esac],
[timing=false])
AM_CONDITIONAL(TIMING, test x"$timing" = x"true")

AC_ARG_ENABLE(threadedreinit,
AS_HELP_STRING([--enable-threadedreinit],
               [enable the threaded reinitialization, default: no]),
[case "${enableval}" in
             yes) threadedreinit=true ;;
             no)  threadedreinit=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-threadedreinit]) ;;
esac],
[threadedreinit=false])
AM_CONDITIONAL(THREADED_REINIT, test x"$threadedreinit" = x"true")

if test x"$threadedprefetch" = x"true" && test x"$pagecache" != x"true"
then
  AC_MSG_ERROR([${threadedprefetch}${pagecache} pagecache should be enabled to enable the threaded prefetch]) ;
fi

AC_ARG_ENABLE(reaperthread,
AS_HELP_STRING([--enable-reaperthread],
               [enable reaperthread, default: no]),
[case "${enableval}" in
             yes) reaperthread=true ;;
             no)  reaperthread=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-reaperthread]) ;;
esac],
[reaperthread=false])
AM_CONDITIONAL(REAPERTHREAD, test x"$reaperthread" = x"true")

AC_ARG_ENABLE(enable_affinity,
AS_HELP_STRING([--enable-affinity],
               [enable affinity, default: yes]),
[case "${enableval}" in
             yes) enable_affinityd=true ;;
             no)  enable_affinity=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-affinity]) ;;
esac],
[enable_affinity=true])
AM_CONDITIONAL(ENABLE_AFFINITY, test x"$enable_affinity" = x"true")

if test x"$threadedprefetch" = x"true" && test x"$pagecache" != x"true"
then
  AC_MSG_ERROR([${threadedprefetch}${pagecache} pagecache should be enabled to enable the threaded prefetch]) ;
fi

AC_PROG_CC
AC_PROG_CXX
AM_PROG_AR()
LT_INIT
AC_OUTPUT(Makefile test/Makefile monitor/Makefile lib/pagecache/Makefile lib/userfault/Makefile lib/externram/Makefile lib/lrubuffer/Makefile lib/monitorstats/Makefile)
