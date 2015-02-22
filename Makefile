LWS_INCLUDE=/path/to/libwebsockets/lib
LWS_LIB=/path/to/libwebsockets/build/lib

all: wspipe

wspipe: wspipe.o
	g++ -L$(LWS_LIB) $^ -lwebsockets -lstdc++ -o $@

wspipe.o: wspipe.cpp
	g++ -I$(LWS_INCLUDE) -g -W -Wall -Wextra -Wnon-virtual-dtor -Wno-unused-local-typedefs -pedantic -pipe -O3 -std=c++11 -c wspipe.cpp -o $@

clean:
	rm -f wspipe wspipe.o

.PHONY: all clean
