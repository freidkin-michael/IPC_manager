/**
 * @file stress_monitor.c
 * @brief Stress monitor that sends commands without checking registration
 *
 * This monitor is designed for testing statistics (timeout counts).
 * It sends commands continuously without checking if configurators are
 * registered, allowing us to generate timeouts when configurators are killed.
 */

#define _GNU_SOURCE // Must be before any includes

#include <signal.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <queue_id>\n", argv[0]);
        printf("  queue_id: 1=DAC, 2=PHY, 3=PRACH\n");
        return 1;
    }

    int queue_id = atoi(argv[1]);
    if (queue_id < 1 || queue_id > 3)
    {
        printf("ERROR: queue_id must be 1, 2, or 3\n");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    char name[64];
    snprintf(name, sizeof(name), "stress-monitor-%d-%d", queue_id, getpid());

    if (!register_monitor(name, queue_id))
    {
        printf("ERROR: Failed to register monitor\n");
        return 1;
    }

    log_msg(LOG_LEVEL_LOG, "Stress Monitor started (queue_id=%d)", queue_id);

    while (!stop)
    {
        // Send command WITHOUT checking registration
        uint32_t cmd_id = set_event(queue_id);
        if (cmd_id == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Queue %d full, retrying...", queue_id);
            sleep(1);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "Sent command %u to queue %d", cmd_id, queue_id);

        // Wait for completion with 3 second timeout
        int result = wait_for_command(cmd_id, 3);
        if (result == 1)
        {
            log_msg(LOG_LEVEL_LOG, "Command %u SUCCESS", cmd_id);
        }
        else if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "Command %u FAILURE", cmd_id);
        }
        else
        {
            log_msg(LOG_LEVEL_ERR, "Command %u TIMEOUT", cmd_id);
        }

        // Send next command after 1 second
        sleep(1);
    }

    log_msg(LOG_LEVEL_LOG, "Stress Monitor exiting");
    return 0;
}
