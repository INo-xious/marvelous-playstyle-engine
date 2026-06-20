CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
INCLUDES := -Iinclude
BUILD := build

.PHONY: all test clean debug

all: $(BUILD)/marvelous-engine

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/marvelous-engine: src/main.cpp src/chess.cpp include/marvelous/chess.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/main.cpp src/chess.cpp -o $@

$(BUILD)/test-engine: tests/test_engine.cpp src/chess.cpp include/marvelous/chess.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_engine.cpp src/chess.cpp -o $@

test: $(BUILD)/test-engine
	./$(BUILD)/test-engine

debug: CXXFLAGS := -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined
debug: clean test

clean:
	rm -rf $(BUILD)
