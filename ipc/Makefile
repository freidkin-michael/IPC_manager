CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -std=c11
LDFLAGS = -pthread

BUILD = ../build
TARGETS = $(BUILD)/configurator_phy $(BUILD)/configurator_dac_prach $(BUILD)/dac_monitor $(BUILD)/phy_prach_monitor $(BUILD)/multi_monitor $(BUILD)/status $(BUILD)/stress_monitor

all: $(BUILD) $(TARGETS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/configurator_phy: configurator_phy.c ipc_mailbox.h
	$(CC) $(CFLAGS) configurator_phy.c -o $(BUILD)/configurator_phy $(LDFLAGS)

$(BUILD)/configurator_dac_prach: configurator_dac_prach.c ipc_mailbox.h
	$(CC) $(CFLAGS) configurator_dac_prach.c -o $(BUILD)/configurator_dac_prach $(LDFLAGS)

$(BUILD)/dac_monitor: dac_monitor.c ipc_mailbox.h
	$(CC) $(CFLAGS) dac_monitor.c -o $(BUILD)/dac_monitor $(LDFLAGS)

$(BUILD)/phy_prach_monitor: phy_prach_monitor.c ipc_mailbox.h
	$(CC) $(CFLAGS) phy_prach_monitor.c -o $(BUILD)/phy_prach_monitor $(LDFLAGS)

$(BUILD)/multi_monitor: multi_monitor.c ipc_mailbox.h
	$(CC) $(CFLAGS) multi_monitor.c -o $(BUILD)/multi_monitor $(LDFLAGS)

$(BUILD)/status: status.c ipc_mailbox.h
	$(CC) $(CFLAGS) status.c -o $(BUILD)/status $(LDFLAGS)

$(BUILD)/stress_monitor: stress_monitor.c ipc_mailbox.h
	$(CC) $(CFLAGS) stress_monitor.c -o $(BUILD)/stress_monitor $(LDFLAGS)

clean:
	rm -f $(BUILD)/configurator_phy $(BUILD)/configurator_dac_prach $(BUILD)/dac_monitor $(BUILD)/phy_prach_monitor $(BUILD)/multi_monitor $(BUILD)/status $(BUILD)/stress_monitor
	rm -f /dev/shm/cfg_shm

test: all
	@echo "Running IPC test suite..."
	@./test.sh

.PHONY: all clean test
