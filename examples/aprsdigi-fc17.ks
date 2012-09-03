# Kickstart template for use by cobbler. $Revision$
# $Log$
# Revision 1.2  2012/09/02 18:17:44  alan
# add base-system to include X, etc. so can run soundmodemconfig
#
# Revision 1.1  2012/08/05 18:48:45  alan
# Initial revision
#
# Revision 1.1  2012/08/05 03:37:14  alan
# Initial revision
#
# updated from fc17 anaconda-ks.cfg 

# shadow passwords, md5 crypt
auth  --useshadow  --enablemd5
# try to get rid of rhgb quiet from boot loader
bootloader --location=mbr --driveorder=sda --append=""
# clear partitions
clearpart --all --drives=sda
# Use text mode install
text
# allow SSH, telnet to 10151,10152
firewall --enabled --ssh --port=10151:tcp,10152:tcp
# don't run the setup agent
firstboot --disable
# System keyboard
keyboard us
# System language
lang en_US.UTF-8
# Use network installation
url --url=$tree
# If any cobbler repo definitions were referenced in the kickstart profile, include them here.
$yum_repo_stanza
# Network information
$SNIPPET('network_config')
# Reboot after installation
reboot
#network --onboot no --device p4p1 --bootproto dhcp --noipv6 --hostname n2fmc-15
timezone --utc America/New_York
# Root password
rootpw --iscrypted XXX
# add initial users
user --name=n2ygk --password=XXX --iscrypted

selinux --enforcing
authconfig --enableshadow --passalgo=sha512

install
# Clear the Master Boot Record
zerombr
# Allow anaconda to partition the system as needed
autopart

# Disable avahi which sends out IP multicasts on the ax.25 interface!
# add appropriate other services here:
services --disabled="avahi-daemon" --enabled="soundmodem,aprsdigi,aprsd,yum-updatesd,ntpd"

%pre
$SNIPPET('log_ks_pre')
$kickstart_start
$SNIPPET('pre_install_network_config')
# Enable installation monitoring
$SNIPPET('pre_anamon')
%end

# Customize this list of packages as needed:
%packages
$SNIPPET('func_install_if_enabled')
# base system:
@core
@base-system
@online-docs
# dev tools
@development-libs
@development-tools
@editors
emacs
# these are in the standard fc17 repo but probabably not in the distro ISO image so a local
# copy of these packages needs to be installed on the cobbler server:
# need this for mkiss.ko:
kernel-modules-extra
kernel-PAE-modules-extra
yum-updatesd
libax25
libax25-devel
ax25-apps
ax25-tools
alsa-utils
alsa-lib
alsa-lib-devel
soundmodem
aprsd
# this is not in a standard distro. Need my own custom repo.
aprsdigi
# don't need this junk XXX - these removes are not working!
-printing
# avahi multicasts on the AX.25 interface!
-avahi*
# do not want alsa-plugins-pulseaudio.  Make sure it didn't get dragged in.
%end

%post
$SNIPPET('log_ks_post')
# Start yum configuration
$yum_config_stanza
# End yum configuration
$SNIPPET('post_install_kernel_options')
$SNIPPET('post_install_network_config')
$SNIPPET('func_register_if_enabled')
$SNIPPET('download_config_files')
$SNIPPET('koan_environment')
$SNIPPET('redhat_register')
$SNIPPET('cobbler_register')
# Enable post-install boot notification
$SNIPPET('post_anamon')

# add host-specific stuff here:
HN=`/bin/hostname`
# disable ssh root login
sed -i.bak -e 's/#PermitRootLogin yes/PermitRootLogin no/g' /etc/ssh/sshd_config
# turn on automatic yum updates
if [ -f /etc/yum/yum-updatesd.conf ];then
 sed -i.bak \
     -e 's/^do_update = no/do_update = yes/' \
     -e 's/^emit_via = dbus/emit_via = syslog/' \
     /etc/yum/yum-updatesd.conf
fi
# wget axports, soundmodem config, sound card defaults
mkdir /etc/ax25
wget "http://$http_server/~alan/aprsdigi-cfg/$HN.asoundstate" -O /var/lib/alsa/asound.state
wget "http://$http_server/~alan/aprsdigi-cfg/$HN.axports" -O /etc/ax25/axports
wget "http://$http_server/~alan/aprsdigi-cfg/$HN.soundmodemconf" -O /etc/ax25/soundmodem.conf
wget "http://$http_server/~alan/aprsdigi-cfg/$HN.aprsdigiconf" -O /etc/ax25/aprsdigi.conf
wget "http://$http_server/~alan/aprsdigi-cfg/$HN.aprsdconf" -O /etc/aprsd/aprsd.conf
# shouldn't need to do this, but...
/sbin/chkconfig soundmodem on
/sbin/chkconfig aprsd on

# Start final steps
$kickstart_done
# End final steps
%end
