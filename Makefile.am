ACLOCAL_AMFLAGS = -I build

SUBDIRS=src

EXTRA_DIST= \
ChangeLog \
README NEWS AUTHORS \
LICENSE.html LICENSE-2.0.txt \
autogen.sh


# Create some text files from HTML sources
LYNX=lynx
HTML_TO_TEXT=TERM=vt100 $(LYNX) -dump

SUFFIXES = .html .txt

.html.txt:
	$(HTML_TO_TEXT) $< > $@

