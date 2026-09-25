EXE      ?= marvelous
CXX      ?= clang++
SRCS     := $(wildcard src/*.cpp)
UNAME_M  := $(shell uname -m)

CXXFLAGS := -std=c++20 -O3 -DNDEBUG -Wall -Wextra -pthread
LDFLAGS  := -pthread

ifeq ($(UNAME_M),arm64)
    ARCHFLAGS ?= -mcpu=native
else ifeq ($(UNAME_M),aarch64)
    ARCHFLAGS ?= -mcpu=native
else
    ARCHFLAGS ?= -march=native
endif

EVALFILE ?= net/marvelous.nnue
ifneq ($(wildcard $(EVALFILE)),)
    CXXFLAGS += -DEVALFILE=\"$(EVALFILE)\"
endif

all: $(EXE)

$(EXE): $(SRCS) $(wildcard src/*.h) $(wildcard $(EVALFILE))
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -flto $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -f $(EXE) *.profraw *.profdata

.PHONY: all clean
