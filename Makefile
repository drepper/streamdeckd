VERSION = 3.4
RELEASE = 1
CC = gcc $(CSTD)
CXX = g++ $(CXXSTD)
INSTALL = install
LN_FS = ln -fs
SED = sed
TAR = tar
MV_F = mv -f
RPMBUILD = rpmbuild
PKG_CONFIG = pkg-config
INKSCAPE = inkscape

CSTD = -std=gnu17
CXXSTD = -std=gnu++2b
CFLAGS = $(OPTS) $(DEBUG) $(CFLAGS-$@) $(WARN)
CXXFLAGS = $(OPTS) $(DEBUG) $(CXXFLAGS-$@) $(WARN)
CPPFLAGS = $(INCLUDES) $(DEFINES)
LDFLAGS = $(LDFLAGS-$@)

DEFINES = $(DEFINES-$@)
INCLUDES = $(shell $(PKG_CONFIG) --cflags $(DEPPKGS))
OPTS = -Og
DEBUG = -g3
WARN = -Wall

LIBS = $(shell $(PKG_CONFIG) --libs $(DEPPKGS)) -lcpprest -lxdo -lpthread

prefix = /usr
bindir = $(prefix)/bin

IFACEPKGS =
DEPPKGS = freetype2 fontconfig Magick++ libutf8proc libconfig++ keylightpp streamdeckpp libcrypto jsoncpp uuid libwebsockets giomm-2.4 xscrnsaver xi xext x11
ALLPKGS = $(IFACEPKGS) $(DEPPKGS)

OBJS = main.o obs.o obsws.o ftlibrary.o buttontext.o resources.o

SVGS = brightness+.svg brightness-.svg color+.svg color-.svg ftb.svg obs.svg \
       scene_live.svg scene_live_off.svg scene_preview.svg scene_preview_off.svg \
       cut.svg auto.svg record.svg record_off.svg stream.svg stream_off.svg \
       transition.svg transition_off.svg scene_live_unused.svg scene_preview_unused.svg \
       right-arrow.svg left-arrow.svg source.svg source_off.svg source_unused.svg \
       ftb-0.svg ftb-12.svg ftb-25.svg ftb-37.svg ftb-50.svg ftb-62.svg ftb-75.svg ftb-87.svg ftb-100.svg \
       transition_unused.svg virtualcam.svg virtualcam_off.svg
PNGS = $(SVGS:.svg=.png) bulb_on.png bulb_off.png bluejeans.png blank.png


all: streamdeckd

streamdeckd: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

resources.xml: Makefile
	@echo '<gresources><gresource prefix="/org/akkadia/streamdeckd/">' > $@-tmp
	@for f in $(PNGS); do printf '  <file>%s</file>\n' "$$f" >> $@-tmp; done
	@echo '</gresource></gresources>' >> $@-tmp
	$(MV_F) $@-tmp $@

resources.c: resources.xml $(PNGS)
	glib-compile-resources --generate-source $<
resources.h: resources.xml $(PNGS)
	glib-compile-resources --generate-header $<

$(SVGS:.svg=.png): %.png: %.svg
	$(INKSCAPE) --export-type=png -o $@ $^

streamdeckd.spec streamdeckd.desktop: %: %.in Makefile
	$(SED) 's/@VERSION@/$(VERSION)/;s/@RELEASE@/$(RELEASE)/;s|@PREFIX@|$(prefix)|' $< > $@-tmp
	$(MV_F) $@-tmp $@

main.o: obs.hh ftlibrary.hh buttontext.hh resources.h
obs.o: obs.hh obsws.hh buttontext.hh ftlibrary.hh
obsws.o: obsws.hh
ftlibrary.o: ftlibrary.hh
buttontext.o: buttontext.hh

pngs: $(SVGS:.svg=.png)

install: streamdeckd streamdeckd.desktop
	$(INSTALL) -D -c -m 755 streamdeckd $(DESTDIR)$(bindir)/streamdeckd
	$(INSTALL) -D -c -m 644 streamdeckd.desktop $(DESTDIR)$(prefix)/share/applications/streamdeckd.desktop
	$(INSTALL) -D -c -m 644 streamdeckd.svg $(DESTDIR)$(prefix)/share/icons/hicolor/scalable/apps/streamdeckd.svg

dist: streamdeckd.spec streamdeckd.desktop $(PNGS)
	$(LN_FS) . streamdeckd-$(VERSION)
	$(TAR) achf streamdeckd-$(VERSION).tar.xz streamdeckd-$(VERSION)/{Makefile,main.cc,obs.cc,obs.hh,obsws.cc,obsws.hh,ftlibrary.cc,ftlibrary.hh,buttontext.cc,buttontext.hh,README.md,streamdeckd.spec,streamdeckd.spec.in,streamdeckd.desktop.in,*.svg,*.png}
	$(RM) streamdeckd-$(VERSION)

srpm: dist
	$(RPMBUILD) -ts streamdeckd-$(VERSION).tar.xz
rpm: dist
	$(RPMBUILD) -tb streamdeckd-$(VERSION).tar.xz

clean:
	$(RM) streamdeckd $(OBJS) streamdeckd.spec streamdeckd.desktop resources.{xml,c,h}

.PHONY: all install pngs dist srpm rpm clean
.ONESHELL:
