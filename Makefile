# Root Makefile - delegates to subdirectories

.PHONY: all clean test help ipc mon

# Default target
all: ipc mon

# Help message
help:
	@echo "Available targets:"
	@echo "  make              - Build everything (ipc + mon)"
	@echo "  make ipc          - Build IPC system"
	@echo "  make ipc-clean    - Clean IPC build"
	@echo "  make ipc-test     - Run IPC tests"
	@echo "  make mon          - Build MON system"
	@echo "  make mon-clean    - Clean MON build"
	@echo "  make mon-test     - Run MON tests"
	@echo "  make clean        - Clean everything"
	@echo "  make test         - Run all tests"

# IPC targets
ipc:
	@echo "==> Building IPC system..."
	@$(MAKE) -C ipc all

ipc-clean:
	@echo "==> Cleaning IPC system..."
	@$(MAKE) -C ipc clean

ipc-test:
	@echo "==> Testing IPC system..."
	@$(MAKE) -C ipc test

# MON targets
mon:
	@echo "==> Building MON system..."
	@$(MAKE) -C mon all

mon-clean:
	@echo "==> Cleaning MON system..."
	@$(MAKE) -C mon clean

mon-test:
	@echo "==> Testing MON system..."
	@$(MAKE) -C mon test

# Global targets
clean: ipc-clean mon-clean
	@echo "==> Cleaning build directory..."
	@rm -rf build

test: ipc-test mon-test
	@echo "==> All tests completed"

# Convenience aliases
build: all
rebuild: clean all
