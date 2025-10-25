#pragma once

void *kalloc(void);
void kfree(void *);
void kinit(void);

// Superpage (2MB page) allocator
void *superalloc(void);
void superfree(void *);
