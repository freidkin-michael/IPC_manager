/**
@file publisher_phy.c
@brief PHY status publisher (1 thread)
*/
#define TYPED_STATUS_MONITOR_IMPLEMENTATION
#include "mon.h"
#include "common.h"
#include <signal.h>
#include <time.h>

volatile sig_atomic_t running = 1;

void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

int main()
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    if (!typed_status_init_publisher())
    {
        fprintf(stderr, "Failed to init publisher\n");
        return 1;
    }

    printf("PHY Publisher started (PID=%d)\n", getpid());

    PhyStatus phy = {.bandwidth = 20000000,
                     .carrier_freq = 2600000000,
                     .tx_power = 23,
                     .modulation = 4, // 64-QAM
                     .timestamp = (uint64_t)time(NULL)};

    // CRITICAL FIX: Removed trailing space in status name
    typed_status_register("phy_status", sizeof(PhyStatus), &phy);

    uint32_t counter = 0;
    while (running)
    {
        sleep(1);

        phy.tx_power = 20 + (counter % 10);
        phy.modulation = 2 + (counter % 3);   // QPSK, 16-QAM, 64-QAM
        phy.timestamp = (uint64_t)time(NULL); // FIXED: corrected typo "times tamp"

        // CRITICAL FIX: Removed trailing space in status name
        typed_status_set("phy_status", &phy);
        printf("[PHY] Updated: tx_power=%u, modulation=%u\n", phy.tx_power, phy.modulation);

        counter++;
    }

    printf("PHY Publisher stopped\n");
    return 0;
}