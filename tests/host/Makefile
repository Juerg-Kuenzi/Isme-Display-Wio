CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1

test: test_core
	./test_core

test_core: test_core.cpp ../../firmware/IsmeDisplay/core.cpp ../../firmware/IsmeDisplay/core.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp ../../firmware/IsmeDisplay/core.cpp

clean:
	rm -f test_core
