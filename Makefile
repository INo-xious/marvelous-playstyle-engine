EXE      ?= marvelous
CXX      ?= clang++
SRCS     := $(wildcard src/*.cpp)
UNAME_M  := $(shell uname -m)

CXXFLAGS := -std=c++20 -O3 -DNDEBUG -Wall -Wextra -pthread
LDFLAGS  := -pthread

ifeq ($(OS),Windows_NT)
    EXE := $(EXE).exe
    LDFLAGS += -static -Wl,--stack,16777216
endif

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

IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -c clang)
ifneq ($(IS_CLANG),0)
    PROFDATA ?= $(shell command -v llvm-profdata 2>/dev/null || xcrun -f llvm-profdata 2>/dev/null)
    PGO_GEN  := -fprofile-instr-generate
    PGO_USE  := -fprofile-instr-use=$(EXE).profdata
    PGO_MERGE = $(PROFDATA) merge -output=$(EXE).profdata *.profraw
else
    PGO_GEN  := -fprofile-generate
    PGO_USE  := -fprofile-use -fno-peel-loops -fno-tracer
    PGO_MERGE = true
endif

pgo: $(SRCS) $(wildcard src/*.h)
	rm -f *.profraw *.profdata *.gcda
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -flto $(PGO_GEN) $(SRCS) -o $(EXE) $(LDFLAGS)
	LLVM_PROFILE_FILE=$(EXE)-%p.profraw ./$(EXE) bench > /dev/null
	$(PGO_MERGE)
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -flto $(PGO_USE) $(SRCS) -o $(EXE) $(LDFLAGS)
	rm -f *.profraw *.profdata *.gcda

clean:
	rm -f $(EXE) *.profraw *.profdata *.gcda

.PHONY: all clean pgo
