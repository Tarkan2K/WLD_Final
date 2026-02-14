CXX = g++
CXXFLAGS = -O3 -std=c++17 -pthread
TARGET = ../bin/recorder
SRC = recorder.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)
	@echo "Build complete: $(TARGET)"

clean:
	rm -f $(TARGET)
