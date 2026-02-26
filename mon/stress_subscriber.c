/**
 * @file stress_subscriber.c
 * @brief Stress test with rapid subscriptions
 */
#define TYPED_STATUS_MONITOR_IMPLEMENTATION
#include "mon.h"
#include "common.h"
#include <signal.h>

volatile sig_atomic_t running = 1;
static uint32_t total_updates = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

void generic_callback(const void* old_data, const void* new_data)
{
    (void)old_data;
    (void)new_data;

    pthread_mutex_lock(&counter_mutex);
    total_updates++;
    pthread_mutex_unlock(&counter_mutex);
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

    printf("Stress Subscriber started (PID=%d)\n", getpid());
    printf("Subscribing to all available statuses...\n");

    // Wait for publishers
    sleep(2);

    // Subscribe multiple times to each status (stress test)
    const char* statuses[] = {"dac_status", "phy_status", "prach_status"};
    const int num_statuses = 3;
    const int subs_per_status = 5;

    int32_t sub_ids[num_statuses * subs_per_status];
    int sub_count = 0;

    for (int i = 0; i < num_statuses; i++)
    {
        for (int j = 0; j < subs_per_status; j++)
        {
            int32_t sub_id = typed_status_subscribe(statuses[i], generic_callback);
            if (sub_id >= 0)
            {
                sub_ids[sub_count++] = sub_id;
                printf("Subscribed to %s (sub_id=%d, priority=%d)\n", statuses[i], sub_id, j);
            }
        }
    }

    printf("\nTotal subscriptions: %d\n", sub_count);
    printf("Monitoring updates...\n\n");

    time_t start = time(NULL);
    time_t last_report = start;
    uint32_t last_count = 0;

    while (running)
    {
        sleep(1);

        time_t now = time(NULL);
        if (now - last_report >= 5)
        {
            pthread_mutex_lock(&counter_mutex);
            uint32_t current_count = total_updates;
            pthread_mutex_unlock(&counter_mutex);

            uint32_t delta = current_count - last_count;
            printf("[STRESS] Total updates: %u (+%u in last 5s, %.1f/sec)\n", current_count, delta, delta / 5.0);

            last_report = now;
            last_count = current_count;
        }
    }

    time_t end = time(NULL);
    time_t duration = end - start;

    pthread_mutex_lock(&counter_mutex);
    uint32_t final_count = total_updates;
    pthread_mutex_unlock(&counter_mutex);

    printf("\n=== Final Statistics ===\n");
    printf("Duration:       %ld seconds\n", duration);
    printf("Total updates:  %u\n", final_count);
    printf("Average rate:   %.2f updates/sec\n", duration > 0 ? (double)final_count / duration : 0.0);
    printf("Subscriptions:  %d\n", sub_count);

    printf("Stress Subscriber stopped\n");
    (void)sub_ids;
    return 0;
}
