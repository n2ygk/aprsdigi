# automake Makefile.am for Laprs (linux APRS(tm)) package
# APRS is a registered trademark of Bob Bruninga, WB4APR
SUFFIXES = .fig .html .8 .pdf
noinst_LIBRARIES = libaprs.a
sbin_PROGRAMS = aprsmon aprsdigi
noinst_PROGRAMS = mic_e_test testparse testmcast fiforead fifowrite
aprsmon_SOURCES = aprsmon.c aprsshm.c aprsshm.h
LDADD = libaprs.a
aprsdigi_SOURCES = aprsdigi.c
mic_e_test_SOURCES = mic_e_test.c
testparse_SOURCES = testparse.c
testmcast_SOURCES = testmcast.c
fiforead_SOURCES = fiforead.c
fifowrite_SOURCES = fifowrite.c
libaprs_a_SOURCES = mic_e.c mic_e.h libax25ext.c libax25ext.h
man_MANS = aprsdigi.8 aprsmon.8 fiforead.8 fifowrite.8
HTMLS = aprsdigi.html aprsmon.html fiforead.html fifowrite.html
noinst_SCRIPTS = $(HTMLS) aprsdigi.spec missing depcomp mkinstalldirs
DOCS = aprsdigi.lsm parse_cooked_ax25.pdf parse_cooked_ax25.fig
EXTRA_DIST= $(MANS) $(SCRIPTS) $(DOCS)

.8.html: 
	$(MAN2HTML)  $< >$@

.fig.pdf:
	fig2dev -L pdf $< $@

backup:
	tar cfz aprsmon.bak $(DISTFILES) RCS
	mcopy aprsmon.bak a:


