# Linux aprsdigi and aprsmon
Copyright (c) 1996,1997,1998,1999,2001,2002,2003,2004,2009,2012,2014 Alan Crosswell

Released under the GNU Public License.  See the file COPYING for details.

Alan Crosswell, N2YGK
n2ygk@weca.org

## Description

*Aprsdigi* is a specialized Amateur Packet Radio (AX.25) UI-frame
digipeater for the Automatic Position Reporting Systems, APRS(tm). It
uses the Linux kernel AX.25 network stack as well as the SOCK_PACKET
facility to listen for packets on one or more radio interfaces (ports)
and repeat those packets -- with several possible modifications -- on
the same or other interfaces. Aprsdigi can also use the Internet to
tunnel connections among other APRS digipeaters and nodes using IPv4
or IPv6 UDP unicast or multicast.

*Aprsdigi* implements conventional packet radio AX.25 digipeating, in
which a packet is digipeated if the next hop (non-repeated) digipeater
("via") callsign matches the AX.25 port's callsign and sub-station ID
(SSID) or an alias callsign and SSID.

There are a number of extensions to conventional digipeating that have
been proposed for use in the APRS community. Some of these features
have been adopted by Terminal Node Controller (TNC) manufacturers,
notably Paccomm and Kantronics. Aprsdigi implements most if not all of
the commercialy adopted and proposed features. See the APRS 1.0
Protocol Specification at www.tapr.org for protocol
documentation. Aprsdigi attempts to minimally comply with the protocol
specification as well as support experimental APRS features. Specific
features implemented include:

- Single-interface conventional UI-frame digipeating.
- Cross-interface digipeating (also known as bridging, routing or gatewaying) and one-to-many fanout.
- Substitution of a digipeated alias with the interface's callsign (typically used to substitute RELAY, WIDE or TRACE aliases).
- WIDEn-n flooding algorithim.
- TRACEn-n route recording.
- Mic-Encoder(tm) support, including SSID-based digipeating, decompression of packets into the conventional APRS MIM format. (The Mic-Encoder compression is also used by other products such as the Kenwood TH-D7A and D700, and TAPR PIC-Encoder).
- TheNet X1J4 node beacon text translation (removal of the lqTheNet X1J4 (alias)rq prefix from the btext).

## Where to find aprsdigi

The official place where new versions are found is:

   https://github.com/n2ygk/aprsdigi

Others may mirror this stuff elsewhere, but I only promise that the
latest will be at the above site.

## More information

If you are not a member of Tucson Amateur Packet Radio (TAPR), consider
joining!  See www.tapr.org

See the aprsdigi.8 and aprsmon.1 man pages.  Aprsdigi is an intelligent
digipeater (see the APRS Protocol Reference, ISBN 0-9644707-6-4, at
http://www.tapr.org).

Aprsmon is deprecated in favor of Dale Heatherington's aprsd,
http://sourceforge.net/projects/aprsd/.

See the file INSTALL for installation instructions.

See the file NEWS for latest news.









