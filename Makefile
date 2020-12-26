CXX = clang++
CXXFLAGS = -std=c++14 -Wall `pkg-config --cflags libndn-cxx` -g
LIBS = `pkg-config --libs libndn-cxx`
SOURCE_OBJS = client_main.o svs.o
PROGRAMS = client
DEPS = common.hpp version-vector.hpp

all: $(PROGRAMS)

svs.o: socket.cpp socket.hpp $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ -c $(LIBS) socket.cpp

client_main.o: client_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $(LIBS) client_main.cpp

client: $(SOURCE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ client_main.o svs.o $(LIBS)

clean:
	rm -f *.o $(PROGRAMS)
