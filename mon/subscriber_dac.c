/**
 * @file subscriber_dac.c
 * @brief DAC status subscriber
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

void dac_callback(const void* old_data, const void* new_data)
{
    const DacStatus* new_dac = (const DacStatus*)new_data;

    if (old_data)
    {
        const DacStatus* old_dac = (const DacStatus*)old_data;
        printf("[DAC SUB] Change detected:\n");
        printf("  Gain: %u -> %u\n", old_dac->gain, new_dac->gain);
        printf("  Amplitude: %u -> %u\n", old_dac->amplitude, new_dac->amplitude);
    }
    else
    {
        printf("[DAC SUB] Initial value:\n");
        printf("  Gain: %u\n", new_dac->gain);
        printf("  Amplitude: %u\n", new_dac->amplitude);
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

    printf("DAC Subscriber started (PID=%d)\n", getpid());

    // Wait for status to be registered
    sleep(1);

    int32_t sub_id = typed_status_subscribe("dac_status", dac_callback);
    if (sub_id < 0)
    {
        fprintf(stderr, "Failed to subscribe to dac_status\n");
        return 1;
    }

    printf("Subscribed to dac_status (sub_id=%d)\n", sub_id);

    while (running)
    {
        sleep(1);
    }

    printf("DAC Subscriber stopped\n");
    return 0;
}
