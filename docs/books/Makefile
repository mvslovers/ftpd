# Builds the FTPD manuals.  Needs typst and the bookmaster submodule
# (git submodule update --init docs/books/bookmaster).

TYPST ?= typst
BM    := bookmaster
# The PDF carries the edition: ml06-0001-1.pdf is the First Edition of ML06-0001.
# The creation date is fixed at 0 so that a build does not depend on the clock
# or on SOURCE_DATE_EPOCH: the same commit and typst give the same PDF.
BOOKS := ml06-0001-1.pdf ml06-0002-1.pdf

.PHONY: all clean site

all: $(BOOKS)

ml06-0001-%.pdf: ml06-0001.typ $(wildcard guide/*.typ) $(wildcard ex/*/*.txt) $(BM)/bookmaster.typ
	$(TYPST) compile --creation-timestamp 0 --root . --font-path $(BM)/fonts --ignore-system-fonts $< $@

ml06-0002-%.pdf: ml06-0002.typ $(wildcard ref/*.typ) $(BM)/bookmaster.typ
	$(TYPST) compile --creation-timestamp 0 --root . --font-path $(BM)/fonts --ignore-system-fonts $< $@

# The web form, as Read the Docs builds it, into site/.
site:
	sh web/build.sh site

clean:
	rm -rf *.pdf site
