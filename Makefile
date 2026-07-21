CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -DLOCALLENS_VERSION=\"0.2.0-rc1\"
LDFLAGS ?= -lsqlite3

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  CXX := /Library/Developer/CommandLineTools/usr/bin/clang++
  SDKROOT := $(shell xcrun --show-sdk-path)
  CXXFLAGS += -DLOCALLENS_USE_OPENSSL=1 -I/opt/homebrew/opt/openssl@3/include
  CXXFLAGS += -isysroot $(SDKROOT)
  CXXFLAGS += -isystem $(SDKROOT)/usr/include/c++/v1
  LDFLAGS += -L/opt/homebrew/opt/openssl@3/lib -lcrypto
else
  CXXFLAGS += -DLOCALLENS_USE_OPENSSL=1
  LDFLAGS += -lcrypto
endif

.PHONY: all test clean

all: build/locallens

build/locallens: src/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test: build/locallens
	tests/run-fixtures.sh ./build/locallens

clean:
	rm -rf build
