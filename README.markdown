# Cloud Policy Engine

-------------------------------------------------------------------------------

## Table of Contents

1. Mandatory Host Dependencies
2. Optional Host Dependencies
3. Building on Specific Distributions
4. Definitions
5. Architecture


## 1) Mandatory Host Dependencies

* [libqb 0.10.1 or later](https://github.com/asalkeld/libqb)

* [pacemaker](http://www.clusterlabs.org/)

* [glib 2.0](http://www.gtk.org/)

* [dbus-glib](http://www.freedesktop.org/wiki/Software/dbus)

* [libxml2](http://xmlsoft.org/)

* [libcurl](http://curl.haxx.se/)

* [libmicrohttpd](http://www.gnu.org/software/libmicrohttpd/)

* [oz 0.7.0 or later](http://www.aeolusproject.org/oz.html)

* An IAAS platform of some type - the developers typically test with OpenStack

## 2) Optional Host Dependencies

* [libssh2)](http://www.libssh2.org/)

 - To build without, ./configure --disable-transport-ssh2

* [qpid (QMFv2)](http://qpid.apache.org/)

 - To build without, ./configure --disable-transport-matahari

* [deltacloud](http://deltalcloud.apache.org/)

 - To build without, ./configure --disable-api-deltacloud

### 1.1) Dependencies to run pcloudsh

* m2crypto
* python-libguestfs
* python-qpid-qmf
* python-daemon

## 2) Building on Specific Distributions

### Installing on Fedora 15:

    fedora15# yum install autoconf automake gcc-c++ glib2-devel libqb-devel \
              dbus-glib-devel libxml2-devel pacemaker-libs-devel libtool-ltdl-devel \
              qpid-cpp-client-devel qpid-qmf-devel libmicrohttpd-devel libcurl-devel
    fedora15# make rpm
    fedora15# rpm -ivh $(arch)/*.rpm

### Installing on Fedora 14:

qpid on f14 is out of date.  libqb is not available in f14.  Please obtain
copies of the source tree from section 1 for those packages and build from
source.  Note on 64 bit systems, qpid does not autodetect the libdir is
64 bit, so --libdir=/usr/lib64 must be specified.

    fedora14# yum install autoconf automake gcc-c++ glib2-devel \
              dbus-glib-devel libxml2-devel pacemaker-libs-devel libtool-ltdl-devel \
              libmicrohttpd-devel libcurl-devel

    fedora14# cd libqb
    fedora14# make rpm
    fedora14# rpm -ivh $(arch)/*.rpm

install qpid, overwriting your default install

    fedora14# make rpm
    fedora14# rpm -ivh $(arch)/*.rpm

## 3) Definitions

_Assembly_ = user defined guest composed of a VM image, Matahari active
             monitoring agent, boot configuration tools, and applications

_Deployable_ = user defined set of assemblies and services

_CPE_ = Cloud Policy Engine, starts and stops DPE's

_DPE_ = Deployable Policy Engine, controls the services in a customer deployment

## 4) Architecture

Description of the program flow given some different scenarios:

### Create new deployment

1. Cloud management software sends cpe the assembly & service config
   in XML (via QMF). Note very simerlar to what pacemaker PE wants.
2. CPE asks upstart/systemd to start a new DPE.
3. CPE stores the config somewhere (DB or file)
4. CPE waits for the DPE QMF agent to be available, then asks it to
   load the config and managemt the deployment.
5. DPE gathers config + state and sends it to the PE
6. DPE performs the actions (using matahari) as instructed by PE


### Destroy deployment

1. Cloud managemt software tells CPE to destroy a deployment
2. CPE asks upstart/systemd to stop the DPE and deletes the config


### DPE dies or gets restarted

1. CPE notices death of CPE and starts a new one
2. CPE waits for the DPE QMF agent to be available, then asks it to
   load the config and managemt the deployment.
3. DPE gathers config + state and sends it to the PE
4. DPE performs the actions (using matahari) as instructed by PE


### Assembly Instance misses heartbeat

1. DPE notices the Assembly has missed a heartbeat.
2. send a QMF event (assembly failure)
3. DPE gathers config + state and sends it to the PE
4. DPE performs the actions (using matahari) as instructed by PE
   (move services to other assemblies)


### User modifies the deployment configuration

1. Cloud management software sends cpe the assembly & service config
   in XML (via QMF). Note very simerlar to what pacemaker PE wants.
2. CPE sees the DPE is already running.
3. CPE stores the config somewhere (DB or file)
4. CPE then notifys the DPE that the config has changed.
5. DPE gathers config + state and sends it to the PE
6. DPE performs the actions (using matahari) as instructed by PE


### User accesses the event log.

1. cloud management software accesses event log
   Since CPE/DPE is only one part of the cloud software the logs
   need to be inserted into a larger picture.
   So we need an API to log important events.

