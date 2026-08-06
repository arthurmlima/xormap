CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -I../../xormap_cpp -I..

.PHONY: check clean

check: check_k512
	./check_k512

check_k512: check_k512.o xormap.o
	$(CXX) $(CXXFLAGS) -o $@ $^

check_k512.o: check_k512.cpp ../../xormap_cpp/xormap.hpp ../test_params.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

xormap.o: ../../xormap_cpp/xormap.cpp ../../xormap_cpp/xormap.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f check_k512 *.o
