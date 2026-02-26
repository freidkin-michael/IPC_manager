/**
 * @file multi_monitor.c
 * @brief Multi-threaded monitor for dedicated queue pair
 */

#define _GNU_SOURCE // Must be before any includes

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
    log_msg(LOG_LEVEL_LOG, "Multi Monitor exiting");
}

static void* prach_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PRACH_CONFIG; // PRACH uses its own queue

    while (!stop)
    {
        int result = send_and_wait(queue_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "Multi Monitor: PRACH configurator not available, queue full, or timeout");
            sleep(2);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "Multi Monitor: sent PRACH command");

        if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Multi Monitor: PRACH failed");
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "Multi Monitor: PRACH completed successfully");
        }

        sleep(4);
    }

    return NULL;
}

static void* phy_thread(void* arg)
{
    (void)arg;
    int queue_id = CMD_PHY_CONFIG; // PHY uses its own queue

    while (!stop)
    {
        int result = send_and_wait(queue_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "Multi Monitor: PHY configurator not available, queue full, or timeout");
            sleep(2);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "Multi Monitor: sent PHY command");

        if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Multi Monitor: PHY failed");
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "Multi Monitor: PHY completed successfully");
        }

        sleep(5);
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    char name[64];
    snprintf(name, sizeof(name), "multi-monitor-%d", getpid());

    // Register with queue_id=0 since this monitor manages multiple queues
    if (!register_monitor(name, 0))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register monitor");
        return 1;
    }

    log_msg(LOG_LEVEL_LOG,
            "Multi Monitor started with PRACH (queue %d) and PHY (queue %d) threads",
            CMD_PRACH_CONFIG,
            CMD_PHY_CONFIG);

    // Start threads
    pthread_t prach_tid, phy_tid;
    if (pthread_create(&prach_tid, NULL, prach_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PRACH thread");
        return 1;
    }

    if (pthread_create(&phy_tid, NULL, phy_thread, NULL) != 0)
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to create PHY thread");
        stop = true;
        pthread_join(prach_tid, NULL);
        return 1;
    }

    // Wait for threads
    pthread_join(prach_tid, NULL);
    pthread_join(phy_tid, NULL);

    return 0;
}
