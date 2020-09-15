VERSION = 0.1

CXX = g++ $(CXXSTD)
INSTALL = install
LN_FS = ln -fs
SED = sed
TAR = tar
MV_F = mv -f
RM_F = rm -f
RPMBUILD = rpmbuild
PKG_CONFIG = pkg-config

CXXSTD = -std=gnu++20
CXXFLAGS = $(OPTS) $(DEBUG) $(CXXFLAGS-$@) $(WARN)
CPPFLAGS = $(INCLUDES) $(DEFINES)
LDFLAGS = $(LDFLAGS-$@)

DEFINES = $(DEFINES-$@)
INCLUDES = $(shell $(PKG_CONFIG) --cflags $(DEPPKGS))
OPTS = -O0
DEBUG = -g3
WARN = -Wall

LIBS = $(shell $(PKG_CONFIG) --libs $(DEPPKGS)) -lcpprest -lpthread

prefix = /usr
bindir = $(prefix)/bin
sharedir = $(prefix)/share/streamdeckd-$(VERSION)

IFACEPKGS = 
DEPPKGS = libconfig++ keylightpp streamdeckpp libcrypto
ALLPKGS = $(IFACEPKGS) $(DEPPKGS)

SVGS = brightness+.svg brightness-.svg color+.svg color-.svg
PNGS = $(SVGS:.svg=.png) bulb_on.png bulb_off.png bluejeans.png

DEFINES-main.o = -DSHAREDIR=\"$(sharedir)\"


all: streamdeckd

streamdeckd: main.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

streamdeckd.spec: streamdeckd.spec.in Makefile
	$(SED) 's/@VERSION@/$(VERSION)/' $< > $@-tmp
	$(MV_F) $@-tmp $@

streamdeckd.desktop: streamdeckd.desktop.in Makefile
	$(SED) 's/@VERSION@/$(VERSION)/;s|@PREFIX@|$(prefix)|' $< > $@-tmp
	$(MV_F) $@-tmp $@

install: streamdeckd streamdeckd.desktop
	$(INSTALL) -D -c -m 755 streamdeckd $(DESTDIR)$(bindir)/streamdeckd
	for p in $(PNGS); do \
	  $(INSTALL) -D -c -m 644 $$p $(DESTDIR)$(sharedir)/$$p; \
	done
	$(INSTALL) -D -c -m 644 streamdeckd.desktop $(DESTDIR)$(prefix)/share/applications/streamdeckd.desktop
	$(INSTALL) -D -c -m 644 streamdeckd.svg $(DESTDIR)$(prefix)/share/icons/hicolor/scalable/apps/streamdeckd.svg

dist: streamdeckd.spec streamdeckd.desktop
	$(LN_FS) . streamdeckd-$(VERSION)
	$(TAR) achf streamdeckd-$(VERSION).tar.xz streamdeckd-$(VERSION)/{Makefile,main.cc,README.md,streamdeckd.spec,streamdeckd.spec.in,streamdeckd.desktop.in,*.svg,*.png}
	$(RM_F) streamdeckd-$(VERSION)

srpm: dist
	$(RPMBUILD) -ts streamdeckd-$(VERSION).tar.xz
rpm: dist
	$(RPMBUILD) -tb streamdeckd-$(VERSION).tar.xz

clean:
	$(RM_F) streamdeck main.o streamdeckd.spec streamdeckd.desktop

.PHONY: all install dist srpm rpm clean
.ONESHELL:
