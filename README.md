# FluidMem: open memory disaggregation
----

FluidMem is an open source platform for decoupling memory from individual servers. It is designed for virtualized cloud platforms
with a high-speed network betweeen hypervisors and a key-value store. A virtual machine's memory can then be offloaded to or pulled in
from a key-value store over the network.

It is built on thse open source components of the Linux kernel:
  1. Userfaultfd
  2. KVM

FluidMem gains flexibility by integrating with other open source projects:
  * Key-value stores such as [RAMCloud](https://ramcloud.atlassian.net/wiki/spaces/RAM/overview) and [memcached](http://www.memcached.org/)
  * Qemu with memory hotplug for on-demand remote memory
  * [OpenStack](https://www.openstack.org/) cloud platform to provide transparent memory expansion to cloud VMs.

Memory disaggregation promises to address the problem of limited memory capacity of datacenter servers, making extra memory a
dynamic entity and presenting it transparently. FluidMem goes beyond existing research in memory disaggregation by implementing
flexible (via user space page fault handling) and comprehensive memory disaggregation (including downsizing memory footprint).
FluidMem integrates with the Linux virtualization stack already used in cloud datacenters.

## Citations

If you find FluidMem useful for any academic research, please include a reference to the [FluidMem paper on arXiv](https://arxiv.org/abs/1707.07780):
> Caldwell, Blake, Youngbin Im, Sangtae Ha, Richard Han, and Eric Keller. "FluidMem:
> Memory as a Service for the Datacenter." arXiv preprint arXiv:1707.07780 (2017).

## Prerequisites
* Linux kernel > 4.3 with remap patches ([custom kernel](https://github.com/blakecaldwell/userfault-kernel/tree/userfault_4.20-rc7))
* Qemu with userfaultfd memory backend ([patching Qemu instructions](patches/qemu))
* A key-value store accessible from the hypervisor ([RAMCloud](https://ramcloud.atlassian.net/wiki/spaces/RAM/overview?mode=global) and [memcached](https://github.com/memcached/memcached/wiki/Install) currently supported)
* [Zookeeper](https://zookeeper.apache.org/) for maintaining cluster state
* Package dependencies: see the 'Requires:' and 'BuildRequires:' lines in the RPM spec files

For integration into a cloud envionment, [libvirt](patches/libvirt) and [OpenStack nova](patches/nova) can be patched to start a VM with the Qemu userfaultfd memory backend

## Installation

### FluidMem monitor on hypervisor
* Base requirements: `boost-devel, gcc-c++, autoconf, automake, libtool, libzookeeper-devel, libzookeeper, boost-system, kernel-headers >= 4.3.0`
* RAMCloud backend requirements: `ramcloud, protobuf-devel, libmlx4`
* memcached backend requirements: `libmemcached-devel, libmemcached`

```
git clone https://github.com/blakecaldwell/fluidmem.git
cd fluidmem
./autogen.sh
KV_BACKEND=ramcloud
./configure --enable-${KV_BACKEND}
make
make install
```
Note that this compiles the FluidMem monitor without prefetch or asynchronous page eviction optimizations. See `./configure --help` for available optimizations

## Running FluidMem

### Start key-value backend 
* [RAMCloud](https://ramcloud.atlassian.net/wiki/spaces/RAM/pages/6848532/Setting+Up+a+RAMCloud+Cluster)
* [memcached]

### Start FluidMem monitor on hypervisor
```
# For memcached
LOCATOR="--SERVER=127.0.0.1"
# For RAMCloud
LOCATOR=zk:10.0.1.1:2181
ZOOKEEPER=10.0.1.1:2181
# set default cache size to 20,000 pages (80MB)
CACHE_SIZE=20000
# monitor will run in the foreground
monitor $LOCATOR --zookeeper=${ZOOKEEPER} --cache_size=${CACHE_SIZE}
```

Note that if prefetch is enabled then monitor should be started with `--enable_prefetch=1`. Additionally `--prefetch_size=` `--page_cache_size=` should be set appropriately

Log messages will be sent to stderr. The status of monitor can be observed by running the ui to retrieve stats:
```
ui 127.0.0.1 s
```

### Add hotpug memory to VM
#### Libvirt
Create an XML file describing the hotplug device /tmp/hotplug.xml:
```
<memory model='dimm'>
  <target>
    <size unit='KiB'>4194304</size>
    <node>0</node>
  </target>
</memory>
```

Attach the hotplug memory device
```
virsh attach-device [instance ID] /tmp/hotplug.xml
```

#### OpenStack (with patches to nova)
1. Find out the UUID of the VM (i.e. nova show [name]).
2. Run the script from the scaleos repository on the hypervisor
```
fluidmem/scripts/attachMemory.py [UUID <memory to add in KB> <NUMA node within VM to hotplug>
```
For example:
```
fluidmem/scripts/attachMemory.py b95dacf8-b84c-41b2-bf2e-ed2ec4ac8ce6 4194304 1
```

Note: Don't restart FluidMem monitor after hotplugging memory.

#### Qemu
Install rlwrap and socat:
```
yum install -y wget
wget https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
rpm -ivh epel-release-latest-7.noarch.rpm
yum install rlwrap socat
```

Connect to Qemu monitor and send hotplug commands:
```
rlwrap -H ~/.qmp_history socat UNIX-CONNECT:/var/lib/libvirt/qemu/domain-instance-000003a1/monitor.sock STDIO
rlwrap: warning: environment variable TERM not set, assuming vt100

warnings can be silenced by the --no-warnings (-n) option
{"QMP": {"version": {"qemu": {"micro": 1, "minor": 2, "major": 2}, "package": ""}, "capabilities": []}}
{"execute":"qmp_capabilities"}
{"return": {}}
{"execute":"object-add","arguments": {"qom-type": "memory-backend-elastic","id": "mem3", "props": {"size": 1073741824}}}
{"return": {}}
{"execute":"device_add", "arguments": {"driver": "pc-dimm","id":"dimm3", "memdev":"mem3","node":0}}
{"return": {}}
{"timestamp": {"seconds": 1543541816, "microseconds": 247405}, "event": "ACPI_DEVICE_OST", "data": {"info": {"device": "dimm3", "source": 1, "status": 0, "slot": "0", "slot-type": "DIMM"}}}
```

### Online memory inside VM (not necessary for all distributions)
Run fluidmem/scripts/online-mem.sh in VM
```
$ sudo ~/fluidmem/scripts/online_mem.sh 
onlined memory63 as zone movable
onlined memory62 as zone movable
onlined memory61 as zone movable
onlined memory60 as zone movable
onlined memory59 as zone movable
onlined memory58 as zone movable
onlined memory57 as zone movable
onlined memory56 as zone movable
onlined memory55 as zone movable
```

### Check that extra memory shows up in VM
Run `free` or `top` within the VM. To see which NUMA node the extra memory is available on, use `numactl -H`
