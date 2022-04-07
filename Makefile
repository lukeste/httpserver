SOURCES=main.cpp httpserver.cpp
INCLUDES=$(wildcard *.h)
TARGET=httpserver
OBJECTS=$(SOURCES:.cpp=.o)
DEPS=$(SOURCES:.cpp=.d)
CXXFLAGS=-std=gnu++11 -Wall -Wextra -Wpedantic -Wshadow -g -O2
CXX=g++

all: $(TARGET)

clean:
	-rm $(DEPS) $(OBJECTS)

spotless: clean
	-rm $(TARGET)

format:
	clang-format -i -style=file $(SOURCES) $(INCLUDES)

valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file="out.valgrind" ./httpserver localhost 8080 -r -N 1

$(TARGET): $(OBJECTS)
	$(CXX) -pthread -o $@ $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -MD -o $@ $<

-include $(DEPS)
