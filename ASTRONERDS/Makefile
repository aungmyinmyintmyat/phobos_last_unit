CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CORE := $(filter-out src/main.cpp src/SDLRenderer.cpp,$(wildcard src/*.cpp))
HEADERS := $(wildcard include/horizon/*.hpp)
SDL_FLAGS := $(shell pkg-config --cflags --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_FLAGS)),)
# This workspace can use the unpacked SDL development headers with the installed
# runtime, without installing system packages or requiring root.
ifneq ($(wildcard build/sdl-dev/root/usr/include/SDL2/SDL.h),)
SDL_FLAGS := -Ibuild/sdl-dev/root/usr/include/SDL2 -Ibuild/sdl-dev/root/usr/include/x86_64-linux-gnu -D_REENTRANT -l:libSDL2-2.0.so.0
endif
endif
ifneq ($(strip $(SDL_FLAGS)),)
GRAPHICS := src/SDLRenderer.cpp
CPPFLAGS += -DHORIZON_HAS_SDL=1
endif
.PHONY: all run test clean sanitize preview-rescue
all: build/horizon
run: build/horizon
	./build/horizon --window
preview-rescue: build/horizon_rescue_tests
	./build/horizon_rescue_tests build/rescue-previews
build:
	mkdir -p build
build/horizon: $(CORE) src/main.cpp $(GRAPHICS) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) src/main.cpp $(GRAPHICS) -o $@ $(SDL_FLAGS)
build/horizon_tests: $(CORE) tests/test_main.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_main.cpp -o $@
build/horizon_battery_tests: $(CORE) tests/test_battery.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_battery.cpp -o $@
build/horizon_environment_tests: $(CORE) tests/test_environment.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_environment.cpp -o $@
test: build/horizon_rescue_tests build/horizon_ship_tests build/horizon_mission1_tests build/horizon build/horizon_tests build/horizon_battery_tests build/horizon_environment_tests
	./build/horizon_rescue_tests
	./build/horizon_ship_tests
	./build/horizon_mission1_tests
	./build/horizon_tests
	./build/horizon_battery_tests
	./build/horizon_environment_tests
	./build/horizon --demo
sanitize: | build
	$(CXX) -Iinclude -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/test_main.cpp -o build/horizon_tests_sanitized
	./build/horizon_tests_sanitized
clean:
	rm -rf build

build/horizon_mission1_tests: $(CORE) tests/test_mission1.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_mission1.cpp -o $@

build/horizon_ship_tests: $(CORE) tests/test_ship.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_ship.cpp -o $@

build/horizon_rescue_tests: $(CORE) tests/test_rescue.cpp $(HEADERS) | build
	$(CXX) -Iinclude $(CXXFLAGS) $(CORE) tests/test_rescue.cpp -o $@
