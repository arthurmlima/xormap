BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
BREW ?= brew
OPENSSL_ROOT_DIR ?= $(shell command -v $(BREW) >/dev/null 2>&1 && $(BREW) --prefix openssl@3 2>/dev/null)
CURL_ROOT ?= $(shell command -v $(BREW) >/dev/null 2>&1 && $(BREW) --prefix curl 2>/dev/null)

CMAKE_DEPENDENCY_ARGS := $(if $(OPENSSL_ROOT_DIR),-DOPENSSL_ROOT_DIR=$(OPENSSL_ROOT_DIR))
CMAKE_DEPENDENCY_ARGS += $(if $(CURL_ROOT),-DCURL_ROOT=$(CURL_ROOT))

.PHONY: all configure build test clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_DEPENDENCY_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
