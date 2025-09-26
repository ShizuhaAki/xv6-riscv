#pragma once

#include "types.h"

int printf(char *fmt, ...);
void panic(char *msg) __attribute__((noreturn));
void printfinit(void);
void print_percent(uint64 percent_x100);
