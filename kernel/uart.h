#pragma once

void uartinit(void);
void uartintr(void);
void uartwrite(char buf[], int n);
void uartputc_sync(int c);
int uartgetc(void);
