CXX=g++
CXXFLAGS=-std=c++11
FLAGS=-lnuma

TARGETS= main simple

all: $(TARGETS)

$(TARGETS):
	$(CXX) $(CXXFLAGS) -o $@ $@.cpp $(FLAGS)

clean:
	rm $(TARGETS)
