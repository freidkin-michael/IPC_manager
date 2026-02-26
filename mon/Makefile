BUILD = ../build

TARGETS = $(BUILD)/publisher_dac_prach \
          $(BUILD)/publisher_phy \
          $(BUILD)/subscriber_dac \
          $(BUILD)/subscriber_phy_prach \
          $(BUILD)/subscriber_multi \
          $(BUILD)/stress_subscriber \
          $(BUILD)/mon_status

CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread -lrt

all: $(TARGETS)

$(BUILD)/publisher_dac_prach: publisher_dac_prach.c mon.h common.h
	gcc $(CFLAGS) -o $@ publisher_dac_prach.c $(LDFLAGS)

$(BUILD)/publisher_phy: publisher_phy.c mon.h common.h
	gcc $(CFLAGS) -o $@ publisher_phy.c $(LDFLAGS)

$(BUILD)/subscriber_dac: subscriber_dac.c mon.h common.h
	gcc $(CFLAGS) -o $@ subscriber_dac.c $(LDFLAGS)

$(BUILD)/subscriber_phy_prach: subscriber_phy_prach.c mon.h common.h
	gcc $(CFLAGS) -o $@ subscriber_phy_prach.c $(LDFLAGS)

$(BUILD)/subscriber_multi: subscriber_multi.c mon.h common.h
	gcc $(CFLAGS) -o $@ subscriber_multi.c $(LDFLAGS)

$(BUILD)/stress_subscriber: stress_subscriber.c mon.h common.h
	gcc $(CFLAGS) -o $@ stress_subscriber.c $(LDFLAGS)

$(BUILD)/mon_status: mon_status.c mon.h common.h
	gcc $(CFLAGS) -o $@ mon_status.c $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/typed_status_shm

test: all
	@echo "Running MON test suite..."
	@./test.sh

.PHONY: all clean test
