CXX = g++
CXXFLAGS = -Wall -std=c++11 -pthread

all: doip_server doip_client

doip_server: doip_server.cpp doip_common.h
	$(CXX) $(CXXFLAGS) -o doip_server doip_server.cpp

doip_client: doip_client.cpp doip_common.h
	$(CXX) $(CXXFLAGS) -o doip_client doip_client.cpp

clean:
	rm -f doip_server doip_client
