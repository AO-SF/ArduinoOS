#ifdef ARDUINO
#ifndef KERNEL_UART_H
#define KERNEL_UART_H

#include <stdio.h>

int uart_putchar(char c, FILE *stream);
char uart_getchar(FILE *stream);

void uart_init(void);

extern FILE uart_output;
extern FILE uart_input;

#endif
#endif
