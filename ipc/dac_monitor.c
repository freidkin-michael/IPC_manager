/**
 * @file dac_monitor.c
 * @brief DAC monitor for dedicated queue pair
 *
 * @details
 * Monitors DAC configurator availability and sends periodic configuration
 * requests. Uses readiness check at startup: waits for is_configurator_ready()
 * before sending commands. Waits gracefully if configurator unavailable instead
 * of auto-restarting.
 *
 * Architecture:
 * - Queue ID: CMD_DAC_CONFIG (1)
 * - Send Interval: 3 seconds
 * - Command Timeout: 5 seconds
 * - Registration Check: kill(pid, 0) liveness validation
 *
 * Behavior:
 * 1. Check if DAC configurator registered
 * 2. If available: send command and wait for completion
 * 3. If unavailable: log warning and wait before retry
 * 4. No automatic restart logic - relies on external supervisor (systemd)
 *
 * Usage:
 * @code
 * ./dac_monitor
 * @endcode
 *
 * @author Michael Freidkin
 * @date 2026
 * @version 2.0
 */

#define _GNU_SOURCE // Must be before any includes

#include <signal.h>

#include "ipc_mailbox.h"

volatile sig_atomic_t stop = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop = 1;
    log_msg(LOG_LEVEL_LOG, "DAC Monitor exiting");
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // DAC monitor always uses CMD_DAC_CONFIG queue
    int queue_id = CMD_DAC_CONFIG;

    char name[64];
    snprintf(name, sizeof(name), "dac_monitor-%d", getpid());

    if (!register_monitor(name, queue_id))
    {
        log_msg(LOG_LEVEL_ERR, "ERROR: Failed to register monitor");
        return 1;
    }

    log_msg(LOG_LEVEL_LOG, "DAC Monitor started (queue_id=%d)", queue_id);

    // Main loop - send_and_wait checks configurator availability internally
    while (!stop)
    {
        int result = send_and_wait(queue_id, 5);
        if (result < 0)
        {
            log_msg(LOG_LEVEL_ERR, "DAC Monitor: not available");
            sleep(1);
            continue;
        }

        log_msg(LOG_LEVEL_LOG, "DAC Monitor: sent DAC command");

        if (result == 0)
        {
            log_msg(LOG_LEVEL_ERR, "DAC Monitor: command failed");
        }
        else
        {
            log_msg(LOG_LEVEL_LOG, "DAC Monitor: command completed successfully");
        }

        sleep(1);
    }

    return 0;
}
