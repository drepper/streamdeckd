VERSION = 1.6

CXX = g++ $(CXXSTD)
INSTALL = install
LN_FS = ln -fs
SED = sed
TAR = tar
MV_F = mv -f
RM_F = rm -f
RPMBUILD = rpmbuild
PKG_CONFIG = pkg-config
INKSCAPE = inkscape

CXXSTD = -std=gnu++20
CXXFLAGS = $(OPTS) $(DEBUG) $(CXXFLAGS-$@) $(WARN)
CPPFLAGS = $(INCLUDES) $(DEFINES)
LDFLAGS = $(LDFLAGS-$@)

DEFINES = $(DEFINES-$@)
INCLUDES = $(shell $(PKG_CONFIG) --cflags $(DEPPKGS))
OPTS = -O0
DEBUG = -g3
WARN = -Wall

LIBS = $(shell $(PKG_CONFIG) --libs $(DEPPKGS)) -lcpprest -lxdo -lpthread

prefix = /usr
bindir = $(prefix)/bin
sharedir = $(prefix)/share/streamdeckd-$(VERSION)

IFACEPKGS = 
DEPPKGS = freetype2 fontconfig Magick++ libutf8proc libconfig++ keylightpp streamdeckpp libcrypto jsoncpp uuid libwebsockets
ALLPKGS = $(IFACEPKGS) $(DEPPKGS)

OBJS = main.o obs.o obsws.o ftlibrary.o buttontext.o

SVGS = brightness+.svg brightness-.svg color+.svg color-.svg ftb.svg obs.svg \
       scene_live.svg scene_live_off.svg scene_preview.svg scene_preview_off.svg \
       cut.svg auto.svg record.svg record_off.svg stream.svg stream_off.svg \
       transition.svg transition_off.svg scene_live_unused.svg scene_preview_unused.svg
PNGS = $(SVGS:.svg=.png) bulb_on.png bulb_off.png bluejeans.png obs.png

DEFINES-main.o = -DSHAREDIR=\"$(sharedir)\"
DEFINES-obs.o = -DSHAREDIR=\"$(sharedir)\"


all: streamdeckd

streamdeckd: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(SVGS:.svg=.png): %.png: %.svg
	$(INKSCAPE) --export-type=png -o $@ $^

streamdeckd.spec streamdeckd.desktop: %: %.in Makefile
	$(SED) 's/@VERSION@/$(VERSION)/' $< > $@-tmp
	$(MV_F) $@-tmp $@

main.o: obs.hh ftlibrary.hh buttontext.hh Makefile
obs.o: obs.hh obsws.hh ftlibrary.hh Makefile
obsws.o: obsws.hh
ftlibrary.o: ftlibrary.hh
buttontext.o: buttontext.hh ftlibrary.hh

pngs: $(SVGS:.svg=.png)

install: streamdeckd streamdeckd.desktop $(PNGS)
	$(INSTALL) -D -c -m 755 streamdeckd $(DESTDIR)$(bindir)/streamdeckd
	for p in $(PNGS); do \
	  $(INSTALL) -D -c -m 644 $$p $(DESTDIR)$(sharedir)/$$p; \
	done
	$(INSTALL) -D -c -m 644 streamdeckd.desktop $(DESTDIR)$(prefix)/share/applications/streamdeckd.desktop
	$(INSTALL) -D -c -m 644 streamdeckd.svg $(DESTDIR)$(prefix)/share/icons/hicolor/scalable/apps/streamdeckd.svg

dist: streamdeckd.spec streamdeckd.desktop $(PNGS)
	$(LN_FS) . streamdeckd-$(VERSION)
	$(TAR) achf streamdeckd-$(VERSION).tar.xz streamdeckd-$(VERSION)/{Makefile,main.cc,obs.cc,obs.hh,obsws.cc,obsws.hh,ftlibrary.cc,ftlibrary.hh,buttontext.cc,buttontext.hh,README.md,streamdeckd.spec,streamdeckd.spec.in,streamdeckd.desktop.in,*.svg,*.png}
	$(RM_F) streamdeckd-$(VERSION)

srpm: dist
	$(RPMBUILD) -ts streamdeckd-$(VERSION).tar.xz
rpm: dist
	$(RPMBUILD) -tb streamdeckd-$(VERSION).tar.xz

clean:
	$(RM_F) streamdeckd $(OBJS) streamdeckd.spec streamdeckd.desktop

.PHONY: all install pngs dist srpm rpm clean
.ONESHELL:
