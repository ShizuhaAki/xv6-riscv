#pragma once

#include "spinlock.h"

extern struct spinlock tickslock;
extern unsigned int ticks;

void trapinit(void);
void trapinithart(void);
void prepare_return(void);
int devintr(void);
