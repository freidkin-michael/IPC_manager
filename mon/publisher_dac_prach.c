/**
 * @file publisher_dac_prach.c
 * @brief DAC + PRACH status publisher (2 threads)
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

void* dac_publisher_thread(void* arg)
{
    (void)arg;

    DacStatus dac = {.gain = 100, .frequency = 2400000, .amplitude = 1000, .enabled = 1, .timestamp = time(NULL)};

    uint32_t counter = 0;
    while (running)
    {
        sleep(2);

        dac.gain = 100 + (counter % 50);
        dac.amplitude = 1000 + (counter % 100);
        dac.timestamp = time(NULL);

        typed_status_set("dac_status", &dac);
        printf("[DAC] Updated: gain=%u, amplitude=%u\n", dac.gain, dac.amplitude);

        counter++;
    }

    return NULL;
}

void* prach_publisher_thread(void* arg)
{
    (void)arg;

    PrachStatus prach = {.preamble_format = 0,
                         .root_sequence = 1,
                         .zero_correlation_zone = 15,
                         .frequency_offset = 0,
                         .timestamp = time(NULL)};

    uint32_t counter = 0;
    while (running)
    {
        sleep(3);

        prach.preamble_format = counter % 4;
        prach.root_sequence = 1 + (counter % 838);
        prach.timestamp = time(NULL);

        typed_status_set("prach_status", &prach);
        printf("[PRACH] Updated: format=%u, root_seq=%u\n", prach.preamble_format, prach.root_sequence);

        counter++;
    }

    return NULL;
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

    printf("DAC+PRACH Publisher started (PID=%d)\n", getpid());

    // Register both statuses before starting threads to avoid race condition
    DacStatus dac_init = {.gain = 100, .frequency = 2400000, .amplitude = 1000, .enabled = 1, .timestamp = time(NULL)};
    PrachStatus prach_init = {.preamble_format = 0,
                              .root_sequence = 1,
                              .zero_correlation_zone = 15,
                              .frequency_offset = 0,
                              .timestamp = time(NULL)};

    if (typed_status_register("dac_status", sizeof(DacStatus), &dac_init) < 0)
    {
        fprintf(stderr, "Failed to register dac_status\n");
    }

    if (typed_status_register("prach_status", sizeof(PrachStatus), &prach_init) < 0)
    {
        fprintf(stderr, "Failed to register prach_status\n");
    }

    pthread_t dac_thread, prach_thread;
    pthread_create(&dac_thread, NULL, dac_publisher_thread, NULL);
    pthread_create(&prach_thread, NULL, prach_publisher_thread, NULL);

    pthread_join(dac_thread, NULL);
    pthread_join(prach_thread, NULL);

    printf("DAC+PRACH Publisher stopped\n");
    return 0;
}
