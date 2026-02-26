/**
 * @file subscriber_multi.c
 * @brief Multi-subscriber for all statuses (DAC + PHY + PRACH)
 */
#define TYPED_STATUS_MONITOR_IMPLEMENTATION
#include "mon.h"
#include "common.h"
#include <signal.h>

volatile sig_atomic_t running = 1;
static uint32_t dac_updates = 0;
static uint32_t phy_updates = 0;
static uint32_t prach_updates = 0;

void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

void dac_callback(const void* old_data, const void* new_data)
{
    (void)old_data;

    const DacStatus* dac = (const DacStatus*)new_data;
    dac_updates++;
    printf("[MULTI] DAC update #%u: gain=%u\n", dac_updates, dac->gain);
}

void phy_callback(const void* old_data, const void* new_data)
{
    (void)old_data;

    const PhyStatus* phy = (const PhyStatus*)new_data;
    phy_updates++;
    printf("[MULTI] PHY update #%u: tx_power=%u\n", phy_updates, phy->tx_power);
}

void prach_callback(const void* old_data, const void* new_data)
{
    (void)old_data;

    const PrachStatus* prach = (const PrachStatus*)new_data;
    prach_updates++;
    printf("[MULTI] PRACH update #%u: format=%u\n", prach_updates, prach->preamble_format);
}

int main()
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    if (!typed_status_init_subscriber())
    {
        fprintf(stderr, "Failed to init subscriber\n");
        return 1;
    }

    printf("Multi-Subscriber started (PID=%d)\n", getpid());
    printf("Monitoring: DAC, PHY, PRACH\n");

    // Wait for statuses to be registered
    sleep(1);

    typed_status_subscribe("dac_status", dac_callback);
    typed_status_subscribe("phy_status", phy_callback);
    typed_status_subscribe("prach_status", prach_callback);

    printf("Subscribed to all statuses\n");

    // Print statistics every 10 seconds
    time_t last_stats = time(NULL);
    while (running)
    {
        sleep(1);

        time_t now = time(NULL);
        if (now - last_stats >= 10)
        {
            printf("\n=== Statistics ===\n");
            printf("DAC updates:   %u\n", dac_updates);
            printf("PHY updates:   %u\n", phy_updates);
            printf("PRACH updates: %u\n", prach_updates);
            printf("Total:         %u\n\n", dac_updates + phy_updates + prach_updates);
            last_stats = now;
        }
    }

    printf("\nFinal Statistics:\n");
    printf("DAC updates:   %u\n", dac_updates);
    printf("PHY updates:   %u\n", phy_updates);
    printf("PRACH updates: %u\n", prach_updates);
    printf("Total:         %u\n", dac_updates + phy_updates + prach_updates);

    printf("Multi-Subscriber stopped\n");
    return 0;
}
