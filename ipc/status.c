/**
 * @file status.c
 * @brief Status monitoring for queue pair architecture
 *
 * @details
 * Displays real-time status of all registered processes and queue pairs.
 * Uses kill(pid, 0) for liveness checking to detect crashed processes.
 * Read-only access to shared memory - no synchronization needed.
 *
 * Output Format:
 * 1. Process Registry:
 *    - PID, Type (MONITOR/CONFIGURATOR), Name, Queue ID
 *    - Status (Active/Dead based on kill() check)
 *    - Commands Processed counter
 *
 * 2. Queue Pairs:
 *    - Queue ID, Command Type name
 *    - Active commands count
 *    - Next command ID (monotonic counter)
 *
 * Health Checking:
 * - Uses kill(pid, 0) to verify process liveness
 * - Returns EPERM (1) if process exists but owned by different user
 * - Returns ESRCH (3) if process doesn't exist
 *
 * Usage:
 * @code
 * ./status
 * @endcode
 *
 * @author Michael Freidkin
 * @date 2026
 * @version 2.0
 */
#define _GNU_SOURCE // Must be before any includes
#include <stdbool.h>

#include "ipc_mailbox.h"

int main(void)
{
    int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (fd < 0)
    {
        printf("ERROR: Shared memory not found. Is the system running?\n");
        return 1;
    }

    SHARED_QUEUE_AUTO SharedQueue* queue = mmap(NULL, sizeof(SharedQueue), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (queue == MAP_FAILED)
    {
        printf("ERROR: Failed to map shared memory\n");
        return 1;
    }

    if (__atomic_load_n(&queue->initialized, __ATOMIC_ACQUIRE) != QUEUE_INITIALIZED)
    {
        printf("ERROR: Shared memory not initialized\n");
        return 1;
    }

    ProcessSlot processes[MAX_PROCESSES];
    memcpy(processes, (void*)queue->processes, sizeof(processes));

    printf("\n=== Configuration System Status ===\n\n");

    printf("Queue Status & Statistics:\n");
    printf("%-6s %-10s %-10s %-10s %-10s %-10s %-10s\n",
           "QUEUE",
           "QUEUED",
           "NEXT_ID",
           "SUCCESS",
           "FAILURE",
           "TIMEOUT",
           "TOTAL");
    printf("%-6s %-10s %-10s %-10s %-10s %-10s %-10s\n",
           "-----",
           "------",
           "-------",
           "-------",
           "-------",
           "-------",
           "-----");

    for (int i = 0; i < MAX_QUEUE_PAIRS; i++)
    {
        uint32_t success = queue->pairs[i].success_count;
        uint32_t failure = queue->pairs[i].failure_count;
        uint32_t timeout = queue->pairs[i].timeout_count;
        uint32_t total = success + failure + timeout;

        // Show row if queue has been used or has pending commands
        if (queue->pairs[i].count > 0 || queue->pairs[i].next_cmd_id > 1 || total > 0)
        {
            printf("%-6d %-10d %-10u %-10u %-10u %-10u %-10u\n",
                   i,
                   queue->pairs[i].count,
                   queue->pairs[i].next_cmd_id,
                   success,
                   failure,
                   timeout,
                   total);
        }
    }

    printf("\nRegistered Processes:\n");
    printf("%-6s %-8s %-25s %-12s %-8s %-8s %-10s\n", "SLOT", "PID", "NAME", "TYPE", "QUEUE", "STATUS", "COMMANDS");
    printf("%-6s %-8s %-25s %-12s %-8s %-8s %-10s\n", "----", "---", "----", "----", "-----", "------", "--------");

    int active_count = 0;
    int active_monitors = 0;
    int active_configurators = 0;
    int dead_count = 0;

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i].active)
        {
            active_count++;
            if (processes[i].type == PROCESS_MONITOR)
                active_monitors++;
            if (processes[i].type == PROCESS_CONFIGURATOR)
                active_configurators++;

            // Check if process is alive using kill(pid, 0)
            bool is_alive = (kill(processes[i].pid, 0) == 0 || errno == EPERM);
            if (!is_alive)
                dead_count++;

            printf("%-6d %-8d %-25s %-12s %-8d %-8s ",
                   i,
                   processes[i].pid,
                   processes[i].name,
                   processes[i].type == PROCESS_CONFIGURATOR ? "CONFIGURATOR" : "MONITOR",
                   processes[i].queue_id,
                   is_alive ? "ACTIVE" : "DEAD");

            if (processes[i].type == PROCESS_CONFIGURATOR)
            {
                printf("%-10u\n", processes[i].commands_processed);
            }
            else
            {
                printf("%-10s\n", "N/A");
            }
        }
    }

    printf("\nSummary:\n");
    printf("  Active Monitors:       %d\n", active_monitors);
    printf("  Active Configurators:  %d\n", active_configurators);
    printf("  Dead Processes:        %d\n", dead_count);
    printf("\n");

    return 0;
}
