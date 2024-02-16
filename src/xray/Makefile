APPNAME = xray
SOURCES = $(shell find . -type f -name '*.cc' |xargs)
HEADERS = $(shell find . -type f -name '*.h' |xargs)

COPT = -Wall -Wextra -Werror -Wsign-compare -Wpointer-arith -Wcast-qual -Wcast-align

CXX = c++ -std=c++11
OPT = -O0 -g3 $(INCLUDES) $(COPT)

LIB = -lXxf86vm -lXrandr -lX11

prefix ?= /usr/local
BINDIR = $(DESTDIR)$(prefix)/bin
INSTALL ?= install

INCLUDES =

.PHONY: all clean indent install

all:	$(APPNAME)
	@echo Done.

install:
	mkdir -p $(BINDIR)
	mkdir -p $(BINDIR)/runtime
	$(INSTALL) xray $(BINDIR)
	@echo Done.

$(APPNAME):$(COBJ1)
	$(CXX) $(OPT) -o $(APPNAME) $(SOURCES) $(LIB)

clean:
	@echo Cleaning up...
	@rm -f core *.o *~ $(APPNAME)
	@echo Done.

indent:
	@indent -i8 -ut -ts8 -l100 -bl -nbfda -blf -br -ce -npsl -il0 $(SOURCES) $(HEADERS)
	@wc -l $(SOURCES) $(HEADERS)
