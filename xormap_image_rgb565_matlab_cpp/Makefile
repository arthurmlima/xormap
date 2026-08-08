BUILD_DIR ?= build
CMAKE ?= cmake
CTEST ?= ctest
BREW ?= brew
OPENSSL_ROOT_DIR ?= $(shell command -v $(BREW) >/dev/null 2>&1 && $(BREW) --prefix openssl@3 2>/dev/null)

CMAKE_ARGS := $(if $(OPENSSL_ROOT_DIR),-DOPENSSL_ROOT_DIR=$(OPENSSL_ROOT_DIR))

.PHONY: all configure build test verify convert run-all sweep sweep-all full clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

verify: build
	./$(BUILD_DIR)/xormap_rgb565 verify

convert: build
	./$(BUILD_DIR)/xormap_rgb565 convert

run-all: build
	./$(BUILD_DIR)/xormap_rgb565 run-all

sweep: build
	./$(BUILD_DIR)/xormap_rgb565 sweep

sweep-all: build
	./$(BUILD_DIR)/xormap_rgb565 sweep-all

full: convert sweep-all

clean:
	$(CMAKE) -E remove_directory $(BUILD_DIR)
