BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
BREW ?= brew
OPENSSL_ROOT_DIR ?= $(shell command -v $(BREW) >/dev/null 2>&1 && $(BREW) --prefix openssl@3 2>/dev/null)

CMAKE_ARGS := $(if $(OPENSSL_ROOT_DIR),-DOPENSSL_ROOT_DIR=$(OPENSSL_ROOT_DIR))

.PHONY: all configure build test verify run-tests clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

verify: build
	./$(BUILD_DIR)/xormap_rgb888 verify

run-tests: build
	./$(BUILD_DIR)/xormap_rgb888 run-tests

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
