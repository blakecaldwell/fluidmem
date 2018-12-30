%define _unpackaged_files_terminate_build 0
%define _missing_doc_files_terminate_build 0
%define _libdir /usr/lib64
%define _bindir /usr/local/bin
%define _includedir /usr/include
%{!?_version:%define _version 2}
%{!?_release:%define _release 1}

Name:           fluidmem-ramcloud
Version:        %{_version}
Release:        %{_release}
Summary:        FluidMem built for RAMCloud externram backend

License:        GPL
URL:            https://github.com/blakecaldwell/fluidmem
Source0:        fluidmem.tar.gz
BuildArch:      x86_64
BuildRequires:  boost-devel, gcc-c++, autoconf, automake, libtool, ramcloud, protobuf-devel, libzookeeper-devel
Requires:       libzookeeper, ramcloud, libmlx4, kernel-headers >= 4.3.0

%description
FluidMem

%prep
%setup -n fluidmem-ramcloud

%build
sh ./autogen.sh
./configure --enable-ramcloud --enable-pagecache --enable-threadedprefetch \
  --enable-threadedwrite --enable-asynread \
  --prefix=/usr --libdir=/usr/lib64 --bindir=/usr/local/bin
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
%{_bindir}/test_cases
%{_bindir}/test_readahead
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
* Sat Dec 01 2018 Blake Caldwell <caldweba@colorado.edu> 0.1.11
- Bringing up to date after aknown good state
* Sat Jan 14 2017 Blake Caldwell <caldweba@colorado.edu> 0.1.10
- Removed pagetracker libraries
* Wed Aug 17 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.9
- Rename and build without any optimizations
* Sat Jul 30 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.8
- Build with enable-threadedwrite
* Tue Jun 21 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.7
- Build with enable-pagecache-zeropageopt
* Sun Jun 12 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.6
- Removed pagetracker from config options
* Thu May 05 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.5
- Included scripts for managing hotplug memory
* Thu Apr 21 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.4
- Added libmonitorstats to generated RPM
* Thu Apr 07 2016 Blake Caldwell <caldweba@colorado.edu> 0.1.0
- Initial RPM release
