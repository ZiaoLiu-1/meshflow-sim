CXX = c++
PYTHON ?= python3
BUILD ?= build/release
OPT ?= -O3 -DNDEBUG
SAN ?=
CPPFLAGS += -Iinclude
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $(OPT) $(SAN)
LDFLAGS += $(SAN)
SOURCES = src/model.cpp src/workloads.cpp src/tick_engine.cpp src/event_engine.cpp
OBJECTS = $(SOURCES:src/%.cpp=$(BUILD)/%.o)

.PHONY: all test fuzz sanitize clean FORCE
all: $(BUILD)/meshflow $(BUILD)/meshflow-tests
$(BUILD):
	mkdir -p $@
FORCE:
$(BUILD)/config: FORCE | $(BUILD)
	@echo '$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS)' > $(BUILD)/config.tmp
	@cmp -s $(BUILD)/config.tmp $@ || mv $(BUILD)/config.tmp $@
	@rm -f $(BUILD)/config.tmp
$(BUILD)/%.o: src/%.cpp $(BUILD)/config Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/tests.o: tests/tests.cpp $(BUILD)/config Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/fuzz.o: tests/fuzz.cpp $(BUILD)/config Makefile | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/meshflow: $(OBJECTS) $(BUILD)/main.o
	$(CXX) $^ $(LDFLAGS) -o $@
$(BUILD)/meshflow-tests: $(OBJECTS) $(BUILD)/tests.o
	$(CXX) $^ $(LDFLAGS) -o $@
$(BUILD)/meshflow-fuzz: $(OBJECTS) $(BUILD)/fuzz.o
	$(CXX) $^ $(LDFLAGS) -o $@
fuzz: $(BUILD)/meshflow-fuzz
	$(BUILD)/meshflow-fuzz
test: all
	$(BUILD)/meshflow-tests
	$(PYTHON) tests/cli_test.py $(BUILD)/meshflow
sanitize:
	$(MAKE) test fuzz BUILD=build/sanitize OPT='-O1 -g -fno-omit-frame-pointer' SAN='-fsanitize=address,undefined -fno-sanitize-recover=all'
clean:
	rm -rf build
-include $(wildcard $(BUILD)/*.d)
