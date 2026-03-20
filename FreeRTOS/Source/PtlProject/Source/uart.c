#include "uart.h"
#define CPACR_REG (*((volatile uint32_t *)0xE000ED88))
#define configENABLE_FPU    1


void UART_init( void )
{
    UART0_BAUDDIV = 16;
    UART0_CTRL = 1;
    CPACR_REG |= ((3UL << 10*2) | (3UL << 11*2));
}

void UART_printf(const char *s) {
    while(*s != '\0') {
        UART0_DATA = (unsigned int)(*s);
        s++;
    }
}

#include <stdint.h>

void UART_print_uint32(uint32_t num) {
    char buf[11]; 
    int i = 10;
    buf[i] = '\0';
    i--;

    if (num == 0) {
        UART_printf("0");
        return;
    }

    while (num > 0) {
        buf[i] = '0' + (num % 10);
        num /= 10;
        i--;
    }

    UART_printf(&buf[i+1]);
}

void UART_PutChar(char c)
{
    if (c == '\n') {
        UART0_DATA = (unsigned int)'\r';
    }

    UART0_DATA = (unsigned int)c;
}


void UART_print_float(float num, int precision) {
    if (num < 0.0f) {
        UART_PutChar('-');
        num = -num;
    }

    uint32_t integer_part = (uint32_t)num;
    UART_print_uint32(integer_part);

    if (precision > 0) {
        UART_PutChar('.');

        float remainder = num - (float)integer_part;

        for (int i = 0; i < precision; i++) {
            remainder *= 10.0f;
            uint32_t digit = (uint32_t)remainder;
            
            UART_PutChar('0' + digit);
            
            remainder -= (float)digit;
        }
    }
}