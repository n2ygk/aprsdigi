#!/bin/sh
echo "bootstraping an aprsdigi development system"
sudo yum makecache fast
sudo yum -y upgrade
sudo yum -y install yum-cron
sudo yum -y install yum-updateonboot
sudo yum -y install yum-utils
# aprsdigi-specific requirements
sudo yum -y group install 'Development Tools'
sudo yum -y install emacs
# install libax25 and friends.
sudo rpm -Uvh https://linuxax25.googlecode.com/svn/downloads/libax25-1.0.5-140.x86_64.rpm
# libx25-devel conflicts with glibc ax25.h!
sudo rpm -Uvh --force https://linuxax25.googlecode.com/svn/downloads/libax25-devel-1.0.5-140.x86_64.rpm
sudo rpm -Uvh https://linuxax25.googlecode.com/svn/downloads/ax25tools-1.0.3-140.x86_64.rpm
echo "your dev environment is built and aprsdigi is in /vagrant"
echo "build aprsdig as follows:"
echo "aclocal"
echo "automake --add-mising"
echo "autoconf"
echo "./configure"
echo "make"








