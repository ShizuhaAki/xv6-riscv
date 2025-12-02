#pragma once

// khugepaged - Kernel daemon for transparent huge page management.

// Initialize and start the khugepaged daemon.
void khugepaged_init(void);

// Main khugepaged daemon loop (runs as kthread).
void khugepaged_main(void *arg);

// Print khugepaged statistics.
void khugepaged_stats(void);
