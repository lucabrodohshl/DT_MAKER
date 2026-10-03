# Makefile to simplify the build process.

# The build directories
BUILD_DIR = build
RELEASE_DIR = release

# Get the number of processor cores
NPROC = $(shell nproc)

# Default target - development build
all: $(BUILD_DIR)/Makefile
	@echo "--- Building in $(BUILD_DIR) with $(NPROC) cores ---"
	$(MAKE) -C $(BUILD_DIR) -j$(NPROC)

# Release target - optimized build without debug output
release: $(RELEASE_DIR)/Makefile
	@echo "--- Building RELEASE in $(RELEASE_DIR) with $(NPROC) cores ---"
	@echo "--- Release build: DEV_MODE=OFF, optimizations enabled ---"
	$(MAKE) -C $(RELEASE_DIR) -j$(NPROC)
	@echo "--- Build complete. Benchmark binaries are in $(RELEASE_DIR)/benchmarks/ ---"
	@echo "    Run them from the repository root, e.g.:  ./$(RELEASE_DIR)/benchmarks/run_CS1"

# Rule to run cmake and generate the Makefile in the build directory (development)
$(BUILD_DIR)/Makefile: CMakeLists.txt
	@echo "--- Configuring DEVELOPMENT build with CMake ---"
	@mkdir -p $(BUILD_DIR)
	@cmake -S . -B $(BUILD_DIR) -DDEV_MODE=ON

# Rule to run cmake and generate the Makefile in the release directory (optimized)
$(RELEASE_DIR)/Makefile: CMakeLists.txt
	@echo "--- Configuring RELEASE build with CMake ---"
	@mkdir -p $(RELEASE_DIR)
	@cmake -S . -B $(RELEASE_DIR) \
		-DDEV_MODE=OFF \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -flto" \
		-DCMAKE_EXE_LINKER_FLAGS_RELEASE="-flto"

# A 'clean' target to remove the build directory
clean: clean-debug clean-release

# A 'clean-release' target to remove the release directory
clean-release:
	@echo "--- Cleaning release directory ---"
	@rm -rf $(RELEASE_DIR)

# Clean everything
clean-debug: 
	@echo "--- Cleaning debug directory ---"
	@rm -rf $(BUILD_DIR)
# Rebuild target to force cmake and then make
rebuild: clean all

# Rebuild release target
rebuild-release: clean-release release



# Phony targets
.PHONY: all clean clean-release clean-debug rebuild rebuild-release release
