# Building and deploying _aprsdigi_

## Introduction

When I first started developing _aprsdigi_ (in the late 1990's!), I had a
dual-boot Intel laptop running some linux distro (most recently Fedora
Core). I had been deploying on real hardware (servers located at
various radio tower sites) using
[Cobbler](https://github.com/cobbler/cobbler) and a crossover Ethernet
cable between the laptop and the server being installed.

My current development hardware is a Mac OS X laptop and I am trying
to modernize and virtualize development using tools 
like vagrant, git, puppet, razor, etc. and
thought I would document my environment here as a reminder to me and
as a means for others to potentially develop and package _aprsdigi_.

## Building a CentOS development VM

I use [Virtualbox](https://www.virtualbox.org/) for the VM
infrastructure and [Vagrant](https://www.vagrantup.com/) to configure
those VMs. Because I'm paranoid (foolish), I didn't want to use the pre-built
boxes from [Hashicorp](https://atlas.hashicorp.com/boxes/search),
instead preferring to build my boxes from a known vanilla
[CentOS](https://www.centos.org/) distro. Then, when I'm finally ready
to deploy _aprsdigi_ on real hardware, I can use
[razor](https://github.com/puppetlabs/razor-server) to deploy
basically the same distro.

### Build a virtualbox image with packer and boxcutter
I want something based on vanilla CentOS 7.1 with the latest updates installed.
I do this with [packer](https://packer.io/) and [CentOS boxcutter]
(https://github.com/boxcutter/) templates. It should be pretty easy to use other
distro templates to similarly build aprsdigi.

```
git clone https://github.com/boxcutter/centos.git
cd centos
cat >Makefile.local <<EOF
CM=puppet
UPDATE=true
EOF
make virtualbox/centos71
make test-virtualbox/centos71
make ssh-virtualbox/centos71
vagrant box add centos71-puppetlatest-1.0.17 box/virtualbox/centos71-puppetlatest-1.0.17.box
```

### Build a vagrant development server
Using the CentOS 7.1 image created with packer and boxcutter, now configure a
[Vagrantfile](Vagrantfile) that uses
a [shell bootstrap](bootstrap-aprsdev.sh) and login:
```
vagrant up
vagrant ssh
Last login: Sat Jun 20 17:39:07 2015 from 10.0.2.2
Welcome to your Packer-built virtual machine.
[vagrant@localhost ~]$ cd /vagrant
```

## Installing pre-reqs: libax25, axtools
Unlike in Fedora Core, the ax25 libraries and tools are not available in any repo
I was able to find for EPEL. The [linux ax25 "unofficial" distros]
(https://linuxax25.googlecode.com) do seem to work. See the
[shell bootstrap](bootstrap-aprsdev.sh) for details.

## Building aprsdigi
_Aprdigi_ (and the RPM SPEC file I use to package it) is built using the [GNU autotools]
(https://www.gnu.org/software/automake/manual/html_node/Autotools-Introduction.html).

Automake, aclocal, configure, etc. need to be configured and run so as to have a
proper Makefile. See the following configuration files:
- [Makefile.am](Makefile.am)
- [configure.ac](configure.ac)

```
aclocal
automake --add-missing
autoconf
./configure
make
```

## Building RPMs
Here's what I do to build the RPMs:
```
cd
mkdir -p rpmbuild/SOURCES
wget -o rpmbuild/SOURCES/aprsdigi-3.10.0.tar.gz https://github.com/n2ygk/aprsdigi/archive/v3.10.0.tar.gz
rpmbuild -ba rpmbuild/SPECS/aprsdigi.spec
```



