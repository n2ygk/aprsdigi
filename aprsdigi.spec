Summary: AX.25 Automatic Position Reporting System aprsdigi and aprsmon
Name: aprsdigi
Version: 2.4.1
Release: 1
Copyright: GPL
Group: Utilities/System
Packager: Alan Crosswell <n2ygk@weca.org>
Buildroot: /var/tmp/aprsdigi
Source: ftp://ftp.tapr.org/aprssig/linux/aprsdigi-%{PACKAGE_VERSION}.tar.gz
Url: http://www.cloud9.net/~alan/ham/aprsdigi.html
Requires: libax25 >= 0.0.7

%changelog
* Fri Dec 28 2001 Alan Crosswell <alan@columbia.edu>
- Try to make a new RPM, but first need to find RPMs for libax25.

* Mon Apr 05 1999 Alan Crosswell <alan@columbia.edu>
- Initial rpm

%description
Aprsdigi is a specialized Amateur Packet Radio (AX.25) UI-frame digipeater
for the Automatic Position Reporting System, APRS(tm).  

Aprsmon collects and displays standard AX.25 UI text frames in a format similar
to that output by a standard TNC in "Monitor ON" mode and is intended
to be used with programs like javAPRS which wish to see a TNC data
stream over a TCP connection.

%prep
%setup -q -n aprsdigi-2.4.1

%build
./configure
make

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install 

%clean
rm -rf $RPM_BUILD_ROOT

%files
%doc README
%doc INSTALL
%doc COPYING
%doc NEWS
%doc ChangeLog
%doc TODO
%doc AUTHORS
%doc aprsdigi.lsm
/usr/sbin/aprsdigi
/usr/sbin/aprsmon
/usr/man/man8/aprsdigi.8
/usr/man/man8/aprsmon.8
