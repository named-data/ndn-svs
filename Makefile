CXX = clang++
CXXFLAGS = -std=c++14 -Wall `pkg-config --cflags libndn-cxx` -lboost_serialization
LIBS = `pkg-config --libs libndn-cxx`
SOURCE_OBJS = client_main.o svs.o version-vector.o
PROGRAMS = client
DEPS = common.hpp version-vector.hpp

all: $(PROGRAMS)

svs.o: socket.cpp socket.hpp $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ -c $(LIBS) socket.cpp

version-vector.o: version-vector.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ -c $(LIBS) version-vector.cpp

client_main.o: client_main.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $(LIBS) client_main.cpp

client: $(SOURCE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ client_main.o svs.o version-vector.o $(LIBS)

clean:
	rm -f *.o $(PROGRAMS)
