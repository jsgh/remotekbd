#CXXFLAGS = -g -O0
CXXFLAGS = -g -O2
LINK.o = $(LINK.cc)

prefix ?= /usr/local

all: kbd-snd kbd-rcv

clean:
	-rm kbd-snd kbd-rcv *.o

uinames.o: uinames.cpp keyhdrs.stamp

poller.o: poller.cpp

kbd-snd.o: kbd-snd.cpp

kbd-rcv.o: kbd-rcv.cpp

kbd-snd: LDLIBS += -lssh

kbd-snd kbd-rcv: uinames.o poller.o

install: all
	install -D kbd-snd $(DESTDIR)$(prefix)/bin/kbd-snd
	install -D kbd-rcv $(DESTDIR)$(prefix)/bin/kbd-rcv

VALPAT=([0-9]+|0[xX][0-9a-fA_F]+)\s*(\/\*.*\*\/|\/\/.*)?\s*$

keyhdrs.stamp:
	perl -ne 'if (/^#define EV_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >ev_codes.h
	perl -ne 'if (/^#define SYN_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >syn_codes.h
	perl -ne 'if (/^#define KEY_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >key_codes.h
	perl -ne 'if (/^#define BTN_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >btn_codes.h
	perl -ne 'if (/^#define REL_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >rel_codes.h
	perl -ne 'if (/^#define ABS_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >abs_codes.h
	perl -ne 'if (/^#define MSC_(\w+)\s+${VALPAT}/) { print "    { ",$$2,", \"",$$1,"\" },\n"; }' </usr/include/linux/input-event-codes.h >msc_codes.h
	touch keyhdrs.stamp

tarball:
	mkdir remotekbd-0.1
	cp Makefile kbd-snd.cpp kbd-rcv.cpp poller.cpp poller.h uinames.cpp uinames.h remotekbd-0.1/
	tar zcvf remotekbd-0.1.tar.gz remotekbd-0.1
	rm -rf remotekbd-0.1

rpm: tarball
	mkdir -p rpm_base/{SPECS,SOURCES,BUILD,RPMS}
	cp remotekbd-0.1.tar.gz rpm_base/SOURCES/
	cp fedora/remotekbd.spec rpm_base/SPECS/
	rpmbuild --define "_topdir $(PWD)/rpm_base/" -bs $(PWD)/rpm_base/SPECS/remotekbd.spec
	rpmbuild --define "_topdir $(PWD)/rpm_base/" -bb $(PWD)/rpm_base/SPECS/remotekbd.spec

deb: tarball
	mkdir -p deb_base
	( cd deb_base ; tar zxvf ../remotekbd-0.1.tar.gz )
	mkdir -p deb_base/remotekbd-0.1/debian
	cp -a debian/* deb_base/remotekbd-0.1/debian/
	( cd deb_base/remotekbd-0.1 && dpkg-buildpackage -us -uc )
