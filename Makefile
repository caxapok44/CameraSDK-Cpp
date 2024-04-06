# Compiler
CXX := g++

# Directories
SRCDIR := ./src
INCDIR := include
LIBDIR := lib
BINDIR := ./bin
OBJDIR := ./obj

# The target binary to compile to
TARGET := $(BINDIR)/CameraSDKTest

# Compiler flags
CXXFLAGS := -Wall -std=c++20 -g -I$(INCDIR)

# Linker flags
LDFLAGS := -L$(LIBDIR) -lCameraSDK -lyaml-cpp

# Source files

SOURCES := $(shell find $(SRCDIR) -name '*.cpp' -or -name '*.c' -or -name '*.s' -or -name '*.cc')

# Object files
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# .PHONY used to prevent make from doing something with a file named 'all' or 'clean'
.PHONY: all clean run

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@



clean:
	rm -rf $(OBJDIR) $(TARGET)
	rm -f  *.h264 *.ts *.m3u8
	rm -rf .cache

run: $(TARGET)
	sudo LD_LIBRARY_PATH=$(LIBDIR) $(TARGET)



# Create the directories used in the build
$(shell mkdir -p $(BINDIR) $(OBJDIR))
