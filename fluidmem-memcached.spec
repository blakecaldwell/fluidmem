%define _unpackaged_files_terminate_build 0
%define _missing_doc_files_terminate_build 0
%define _libdir /usr/lib
%define _bindir /usr/local/bin
%define _includedir /usr/include
%{!?_version:%define _version 1}
%{!?_release:%define _release 10}

Name:           fluidmem-memcached
Version:        %{_version}
Release:        %{_release}
Summary:        RPM spec file for FluidMem

License:        GPL
URL:            https://github.com/blakecaldwell/fluidmem
Source0:        fluidmem.tar.gz
BuildArch:      x86_64
BuildRequires:  boost-devel, libmemcached-devel, gcc-c++, autoconf, automake, libtool, libzookeeper-devel
Requires:       libzookeeper, libmemcached, kernel-headers >= 4.3.0

%description
FluidMem

%package client
Summary:        Client library of FluidMem

%description client
Client-side including library to connect to fluidmem monitor

%package -n fluidmem-patches
Summary:        Patches to various components of FluidMem

%description -n fluidmem-patches
Patches used for CI

%prep
%setup -n fluidmem-memcached

%build
sh ./autogen.sh
./configure --enable-memcached --enable-pagecache --enable-pagecache-zeropageopt --enable-threadedprefetch \
  --enable-threadedwrite --prefix=/usr --bindir=/usr/local/bin
make

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%post client
/sbin/ldconfig
%postun client
/sbin/ldconfig

%files
%defattr(-,root,root)
%exclude %{_includedir}/userfault-client.h
%{_bindir}/monitor
%{_bindir}/ui
%{_bindir}/test
%{_bindir}/test_readahead
%{_bindir}/test_cases
%{_includedir}/*.h
%{_libdir}/libuserfault.so*
%{_libdir}/liblrubuffer.so*
%{_libdir}/libexternram.so*
%{_libdir}/libpagecache.so*
%{_libdir}/libmonitorstats.so*
%{_datadir}/fluidmem/scripts/attachMemory.py
%{_datadir}/fluidmem/scripts/online_mem.sh
%{_datadir}/fluidmem/tests/*.sh

%files client
%defattr(-,root,root)
%{_includedir}/userfault-client.h
%{_includedir}/dbg.h
%{_libdir}/libuserfault_client.so*

%files -n fluidmem-patches
%defattr(-,root,root)
%{_datadir}/qemu/*.patch
%{_datadir}/libvirt/*.patch
%{_datadir}/nova/*.patch
%{_datadir}/ramcloud/*.patch
#%{_datadir}/kernel/*.patch

#%doc

%changelog
* Fri Dec 21 2018 Blake Caldwell <caldweba@colorado.edu> 0.2.0
- FluidMem release
* Sat Dec 01 2018 Blake Caldwell <caldweba@colorado.edu> 0.1.10
- Bringing up to date after aknown good state
* Sat Jan 14 2017 Blake Caldwell <caldweba@colorado.edu> 0.1.9
- Removed pagetracker library
* Wed Aug 17 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.8
- Build with enable-threadedprefetch
* Sat Jul 30 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.7
- Build with enable-threadedwrite
* Tue Jun 21 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.6
- Build with enable-pagecache-zeropageopt
* Sun Jun 12 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.5
- Removed pagetracker from config options
* Thu May 05 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.4
- Added scripts for managing hotplug memory
* Thu Apr 21 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.3
- Added libmonitorstats to generated RPM
* Mon Feb 15 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.2
- Tweaked file after compiling qemu with client
* Wed Feb 10 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.1
- Working RPM release
* Wed Jan 13 2016 Ravi <naga.elluri@colorado.edu> 1.0
- Initial RPM release
