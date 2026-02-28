PROJECT   ?= hello-triangle
BUILD_DIR := build

# Executable paths (Visual Studio multi-config generator layout)
DEBUG_EXE   := $(BUILD_DIR)/$(PROJECT)/Debug/$(PROJECT).exe
RELEASE_EXE := $(BUILD_DIR)/$(PROJECT)/Release/$(PROJECT).exe
SRC_DIR     := $(PROJECT)

.PHONY: configure build-debug build-release run-debug run-release test help

# ---------------------------------------------------------------------------
# configure: CMake project generation (run once, safe to re-run)
# ---------------------------------------------------------------------------
configure:
	cmake -B $(BUILD_DIR)

# ---------------------------------------------------------------------------
# build-debug / build-release
#   Checks that the source directory exists before building.
# ---------------------------------------------------------------------------
build-debug:
	@if [ -d "$(SRC_DIR)" ]; then \
		cmake -B $(BUILD_DIR) && \
		cmake --build $(BUILD_DIR) --config Debug --target $(PROJECT); \
	else \
		echo "[ERROR] Project '$(PROJECT)' does not exist."; \
		exit 1; \
	fi

build-release:
	@if [ -d "$(SRC_DIR)" ]; then \
		cmake -B $(BUILD_DIR) && \
		cmake --build $(BUILD_DIR) --config Release --target $(PROJECT); \
	else \
		echo "[ERROR] Project '$(PROJECT)' does not exist."; \
		exit 1; \
	fi

# ---------------------------------------------------------------------------
# run-debug / run-release
#   Checks that the executable exists before launching.
# ---------------------------------------------------------------------------
run-debug:
	@if [ -f "$(DEBUG_EXE)" ]; then \
		"$(DEBUG_EXE)"; \
	else \
		echo "[ERROR] Debug executable for '$(PROJECT)' not found."; \
		echo "        Run: make build-debug PROJECT=$(PROJECT)"; \
		exit 1; \
	fi

run-release:
	@if [ -f "$(RELEASE_EXE)" ]; then \
		"$(RELEASE_EXE)"; \
	else \
		echo "[ERROR] Release executable for '$(PROJECT)' not found."; \
		echo "        Run: make build-release PROJECT=$(PROJECT)"; \
		exit 1; \
	fi

# ---------------------------------------------------------------------------
# test: Run CTest for the specified project (Debug config)
# ---------------------------------------------------------------------------
test:
	@if [ -d "$(SRC_DIR)" ]; then \
		cd $(BUILD_DIR) && ctest -C Debug --output-on-failure -R "$(PROJECT)"; \
	else \
		echo "[ERROR] Project '$(PROJECT)' does not exist."; \
		exit 1; \
	fi

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------
help:
	@echo ""
	@echo "Usage: make <target> [PROJECT=<name>]"
	@echo ""
	@echo "  build-debug    PROJECT=<name>   Build debug configuration"
	@echo "  build-release  PROJECT=<name>   Build release configuration"
	@echo "  run-debug      PROJECT=<name>   Run debug executable"
	@echo "  run-release    PROJECT=<name>   Run release executable"
	@echo "  test           PROJECT=<name>   Run CTest (Debug)"
	@echo "  configure                       CMake configure (generate build files)"
	@echo ""
	@echo "Examples:"
	@echo "  make build-debug   PROJECT=hello-triangle"
	@echo "  make build-release PROJECT=hello-triangle"
	@echo "  make run-debug     PROJECT=hello-triangle"
	@echo "  make run-release   PROJECT=hello-triangle"
	@echo "  make test          PROJECT=hello-triangle"
	@echo ""
