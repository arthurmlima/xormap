CXX ?= c++
CXXFLAGS ?= -std=c++17 -O0 -g -Wall -Wextra -Wpedantic
CPPFLAGS ?= -I/opt/homebrew/include

.PHONY: test clean

test: xormap_test
	./xormap_test

xormap_test: test_xormap.o xormap.o
	$(CXX) $(CXXFLAGS) -o $@ $^

xormap.o: xormap.cpp xormap.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

test_xormap.o: test_xormap.cpp xormap.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

clean:
	rm -f xormap_test *.o
