# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.
# -----
# Build all: make
# Clean all: make clean

CXX = clang++ # g++

# O1 or above may change the call stack, e.g. inline calls.
CXX_OPTIMIZE = -O3
CXX_NO_OPTIMIZE = -O0

# Uncomment this to use libc++
# https://gist.github.com/Leedehai/4ed513a8efa835f09ebb1ed8b71eb43f
USE_LIB_CXX = #-stdlib=libc++

CXXFLAGS = -std=c++17 -Wall -pedantic -Iinclude -Isrc -MMD $(USE_LIB_CXX)
LDFLAGS = $(USE_LIB_CXX)

SOLIB_HIDE_SYMBOLS=-fvisibility=hidden -fvisibility-inlines-hidden

# Headers:
#
# I probably should use -MMD or whatever flags to get the dependencies for *.h
# and somehow include the deps into the compile rules. However, the usual trick
# to do it gets entangled with my requirement that all build artifacts should
# go to a subdirectory "out" instead of being mixed with the source files.
# When I was in high school I was very good at Make until I discovered CMake
# and Chromium's GN and TensorFlow's Bazel.
#
# This is one point I don't like Make. So I switched to GN + Ninja for
# larger endeavors: https://gist.github.com/Leedehai/5a0fba275543891f192b92868ee603c0
# I kept Make for this project just to make it handy. Now I don't feel
# like sinking time into making the header dependency work.

all: out/example_demangle out/example_symbolize out/example_symbolize_with_so
	@printf "\033[36mDone: $@\033[0m\n"

clean:
	rm -rf out

out_dir:
	@if [ ! -d out ]; then mkdir out; fi

out/demangler.o : src/demangler.cc | out_dir
	$(CXX) $(CXXFLAGS) $(CXX_OPTIMIZE) -c $^ -o $@

out/example_demangle.o : example/demangle.cc | out_dir
	$(CXX) $(CXXFLAGS) $(CXX_OPTIMIZE) -c $^ -o $@

out/example_demangle : out/example_demangle.o out/demangler.o | out_dir
	$(CXX) $(LDFLAGS) $^ -o $@

out/symbolizer.o : src/symbolizer.cc | out_dir
	$(CXX) $(CXXFLAGS) $(CXX_OPTIMIZE) -c $^ -o $@

out/symbolizer.pic.o : src/symbolizer.cc | out_dir
	$(CXX) $(CXXFLAGS) $(CXX_OPTIMIZE) $(SOLIB_HIDE_SYMBOLS) -fPIC -c $^ -o $@

out/symbolizer.so : out/symbolizer.pic.o | out_dir
	$(CXX) -shared $^ -o $@

out/example_symbolize.o : example/symbolize.cc | out_dir
	$(CXX) $(CXXFLAGS) $(CXX_NO_OPTIMIZE) -c $^ -o $@

out/example_symbolize : out/example_symbolize.o out/symbolizer.o | out_dir
	$(CXX) $(LDFLAGS) $^ -o $@

# This links with the dynamic library. At the current configuration this
# executable must be invoked from the project root, since I did not tell
# the runtime loader how to locate the dynamic libs by tweaking compiler
# and linker flags. To see how to do so, see toolchain/BUILD.gn in my
# another repo https://github.com/leedehai/buildconfig
out/example_symbolize_with_so : out/example_symbolize.o out/symbolizer.so | out_dir
	$(CXX) $(LDFLAGS) -o $@ $^

.PHONY: all clean
