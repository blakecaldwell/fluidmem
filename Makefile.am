ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib/monitorstats lib/externram lib/lrubuffer lib/pagecache lib/userfault monitor test
EXTRA_DIST = autogen.sh

include_HEADERS = include/dbg.h include/pagehash.h include/usage.h
qemudir = $(datadir)/qemu
libvirtdir = $(datadir)/libvirt
kerneldir = $(datadir)/kernel
ramclouddir = $(datadir)/ramcloud
novadir = $(datadir)/nova
scriptsdir = $(datadir)/fluidmem/scripts
testdir= $(datadir)/fluidmem/tests

qemu_DATA = $(shell find patches/qemu/ -maxdepth 1 -type f)
libvirt_DATA = $(shell find patches/libvirt/ -maxdepth 1 -type f)
kernel_DATA = $(shell find patches/kernel/ -maxdepth 1 -type f)
ramcloud_DATA = patches/RAMCloud/*
nova_DATA = patches/nova/*
scripts_DATA = scripts/*
test_DATA = test/*.sh

dist_bin_SCRIPTS = scripts/rebuild_fluidmem_debug.sh

ramcloud-dev-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-ramcloud ; \
	rpmbuild --clean -D "_release `date '+%Y%m%d%H%M%S'`git`git log --pretty=format:'%h' -n 1`" -bb fluidmem-ramcloud.spec

memcached-dev-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-memcached ; \
	rpmbuild --clean -D "_release `date '+%Y%m%d%H%M%S'`git`git log --pretty=format:'%h' -n 1`" -bb fluidmem-memcached.spec

noop-dev-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-noop ; \
	rpmbuild --clean -D "_release `date '+%Y%m%d%H%M%S'`git`git log --pretty=format:'%h' -n 1`" -bb fluidmem-noop.spec

ramcloud-prod-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            --transform 's!^fluidmem-ramcloud-release\(\$\|/\)!fluidmem-ramcloud\1!' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-ramcloud-release ; \
	rpmbuild --clean -bb fluidmem-ramcloud.spec

memcached-prod-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            --transform 's!^fluidmem-memcached-release\(\$\|/\)!fluidmem-memcached\1!' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-memcached-release ; \
	rpmbuild --clean -bb fluidmem-memcached.spec

noop-prod-rpmbuild:
	mkdir -p ~/rpmbuild/SOURCES && \
        tar -C ../ -c --exclude='.git*' --exclude='autom4te.cache' \
            --exclude='vagrant-scalevm' \
            --exclude='doc' --exclude='*.P*' --exclude='*.la' --exclude='*.a' \
            --transform 's!^fluidmem-noop-release\(\$\|/\)!fluidmem-noop\1!' \
            -f ~/rpmbuild/SOURCES/fluidmem.tar.gz fluidmem-noop-release ; \
	rpmbuild --clean -bb fluidmem-noop.spec

rpms-ramcloud: ramcloud-prod-rpmbuild
rpms-memcached: memcached-prod-rpmbuild
rpms-noop: noop-prod-rpmbuild
