APPNAME = xray
SOURCES = $(shell find . -type f -name '*.cc' |xargs)
HEADERS = $(shell find . -type f -name '*.h' |xargs)

COPT = -Wall -Wsign-compare -Wpointer-arith -Wcast-qual -Wcast-align

CXX = c++ -std=c++11
OPT = -O0 -g3 $(INCLUDES) $(COPT)

LIB = -lX11 -lXxf86vm -lXrandr

INCLUDES =

all:	$(APPNAME)
	@echo OK.

$(APPNAME):$(COBJ1)
	$(CXX) $(OPT) -o $(APPNAME) $(SOURCES) $(LIB)

clean:
	@echo Cleaning up...
	@rm -f core *.o *~ $(APPNAME)
	@echo Done.

indent:
	@indent -i8 -ut -ts8 -l100 -bl -nbfda -blf -br -ce -npsl -il0 $(SOURCES) $(HEADERS)
	@wc -l $(SOURCES) $(HEADERS)
