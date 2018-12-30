%define _unpackaged_files_terminate_build 0
%define _missing_doc_files_terminate_build 0
%define _libdir /usr/lib
%define _bindir /usr/local/bin
%define _includedir /usr/include
%{!?_version:%define _version 2}
%{!?_release:%define _release 1}

Name:           fluidmem-noop
Version:        %{_version}
Release:        %{_release}
Summary:        RPM spec file for FluidMem

License:        GPL
URL:            https://github.com/blakecaldwell/fluidmem
Source0:        fluidmem.tar.gz
BuildArch:      x86_64
BuildRequires:  boost-devel, gcc-c++, autoconf, automake, libtool, libzookeeper-devel
Requires:       libzookeeper, boost-system, kernel-headers >= 4.3.0

%description
FluidMem

%prep
%setup -n fluidmem-noop

%build
sh ./autogen.sh
./configure --enable-noop --enable-pagecache --enable-threadedprefetch \
  --enable-threadedwrite --enable-asynread --prefix=/usr --bindir=/usr/local/bin
make

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
%exclude %{_includedir}/userfault-client.h
%{_bindir}/monitor
%{_bindir}/ui
%{_bindir}/test_for_corruption
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

#%doc

%changelog
* Fri Dec 29 2018 Blake Caldwell <caldweba@colorado.edu> 0.2.1
- Rename test to test_for_corruption
* Fri Dec 21 2018 Blake Caldwell <caldweba@colorado.edu> 0.2.0
- FluidMem release
* Sat Dec 01 2018 Blake Caldwell <caldweba@colorado.edu> 0.1.2
- Bringing up to date after aknown good state
* Sat Jan 14 2017 Blake Caldwell <caldweba@colorado.edu> 0.1.1
- Removed pagetracker library
* Mon Dec 12 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.0
- Initial RPM release
