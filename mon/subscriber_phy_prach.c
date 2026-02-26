/**
@file subscriber_phy_prach.c
@brief PHY + PRACH status subscriber (2 subscriptions)
*/
#define TYPED_STATUS_MONITOR_IMPLEMENTATION
#include "mon.h"
#include "common.h"
#include <signal.h>

volatile sig_atomic_t running = 1;

void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

void phy_callback(const void* old_data, const void* new_data)
{
    const PhyStatus* new_phy = (const PhyStatus*)new_data;
    if (old_data)
    {
        const PhyStatus* old_phy = (const PhyStatus*)old_data;
        printf("[PHY SUB] Change: tx_power=%u->%u, mod=%u->%u\n",
               old_phy->tx_power,
               new_phy->tx_power,
               old_phy->modulation,
               new_phy->modulation);
    }
    else
    {
        printf("[PHY SUB] Initial: tx_power=%u, mod=%u\n", new_phy->tx_power, new_phy->modulation);
    }
}

void prach_callback(const void* old_data, const void* new_data)
{
    const PrachStatus* new_prach = (const PrachStatus*)new_data;
    if (old_data)
    {
        const PrachStatus* old_prach = (const PrachStatus*)old_data;
        printf("[PRACH SUB] Change: format=%u->%u, root_seq=%u->%u\n",
               old_prach->preamble_format,
               new_prach->preamble_format,
               old_prach->root_sequence,
               new_prach->root_sequence);
    }
    else
    {
        printf("[PRACH SUB] Initial: format=%u, root_seq=%u\n", new_prach->preamble_format, new_prach->root_sequence);
    }
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

    printf("PHY+PRACH Subscriber started (PID=%d)\n", getpid());

    // Wait for statuses to be registered by publishers
    sleep(1);

    int32_t phy_sub = typed_status_subscribe("phy_status", phy_callback);
    int32_t prach_sub = typed_status_subscribe("prach_status", prach_callback);

    if (phy_sub < 0 || prach_sub < 0)
    {
        fprintf(stderr, "Failed to subscribe\n");
        return 1;
    }

    printf("Subscribed to phy_status (%d) and prach_status (%d)\n", phy_sub, prach_sub);

    while (running)
    {
        sleep(1);
    }

    printf("PHY+PRACH Subscriber stopped\n");
    return 0;
}