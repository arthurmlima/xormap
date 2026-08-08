CMAKE ?= cmake
CTEST ?= ctest

GRAY_DIR := xormap_image_matlab_cpp
RGB888_DIR := xormap_image_rgb888_matlab_cpp
RGB565_DIR := xormap_image_rgb565_matlab_cpp

.PHONY: all build color test verify convert-rgb565 rgb888-sweep \
	rgb888-analysis rgb565-run-all rgb565-sweep rgb565-sweep-all \
	full-color sanitize clean

all: build

build:
	$(MAKE) -C $(GRAY_DIR) build
	$(MAKE) -C $(RGB888_DIR) build
	$(MAKE) -C $(RGB565_DIR) build

color:
	$(MAKE) -C $(RGB888_DIR) build
	$(MAKE) -C $(RGB565_DIR) build

test:
	$(MAKE) -C $(GRAY_DIR) test
	$(MAKE) -C $(RGB888_DIR) test
	$(MAKE) -C $(RGB565_DIR) test

verify:
	$(MAKE) -C $(GRAY_DIR) build
	./$(GRAY_DIR)/build/xormap_gray verify
	$(MAKE) -C $(RGB888_DIR) verify
	$(MAKE) -C $(RGB565_DIR) verify

convert-rgb565:
	$(MAKE) -C $(RGB565_DIR) convert

rgb888-sweep:
	$(MAKE) -C $(RGB888_DIR) sweep

rgb888-analysis:
	$(MAKE) -C $(RGB888_DIR) analysis

rgb565-run-all:
	$(MAKE) -C $(RGB565_DIR) run-all

rgb565-sweep:
	$(MAKE) -C $(RGB565_DIR) sweep

rgb565-sweep-all:
	$(MAKE) -C $(RGB565_DIR) sweep-all

full-color: convert-rgb565 rgb888-sweep rgb888-analysis rgb565-run-all \
	rgb565-sweep rgb565-sweep-all

sanitize:
	$(CMAKE) -S $(RGB888_DIR) -B $(RGB888_DIR)/build-sanitize \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
	$(CMAKE) --build $(RGB888_DIR)/build-sanitize --parallel
	$(CTEST) --test-dir $(RGB888_DIR)/build-sanitize --output-on-failure
	$(CMAKE) -S $(RGB565_DIR) -B $(RGB565_DIR)/build-sanitize \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
	$(CMAKE) --build $(RGB565_DIR)/build-sanitize --parallel
	$(CTEST) --test-dir $(RGB565_DIR)/build-sanitize --output-on-failure

clean:
	$(MAKE) -C $(GRAY_DIR) clean
	$(MAKE) -C $(RGB888_DIR) clean
	$(MAKE) -C $(RGB565_DIR) clean
	$(CMAKE) -E remove_directory $(RGB888_DIR)/build-sanitize
	$(CMAKE) -E remove_directory $(RGB565_DIR)/build-sanitize
