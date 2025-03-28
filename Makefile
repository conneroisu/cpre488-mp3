# Makefile for Target Recognition Development

# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++14 -O2

# OpenCV configuration
OPENCV_CFLAGS = $(shell pkg-config --cflags opencv4)
OPENCV_LIBS = $(shell pkg-config --libs opencv4)

# Target executable
TARGET = dev_recognition

# Source files
SOURCES = ./dev_recog.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Build rules
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -o $@ $^ $(OPENCV_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin

.PHONY: all clean install

# Usage examples
help:
	@echo "Makefile for Target Recognition Development"
	@echo "==========================================="
	@echo "Available targets:"
	@echo "  all      - Build the recognition application (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install the application to /usr/local/bin"
	@echo "  help     - Display this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  To build the application:"
	@echo "    make"
	@echo ""
	@echo "  To run with webcam:"
	@echo "    ./$(TARGET)"
	@echo ""
	@echo "  To run with a specific camera (e.g., camera ID 1):"
	@echo "    ./$(TARGET) -c 1"
	@echo ""
	@echo "  To run with a static image file:"
	@echo "    ./$(TARGET) -i path/to/image.jpg"
	@echo ""
	@echo "Application controls:"
	@echo "  q/ESC - Quit"
	@echo "  f     - Simulate firing"
	@echo "  r     - Reset launcher position"
	@echo "  s     - Save current frame"
	@echo "  h     - Display help message"
