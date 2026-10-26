#include "ts_uart.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>

#define UART0_ADDRESS (0x40004000UL)
#define UART0_DATA (*(((volatile uint32_t *)(UART0_ADDRESS + 0UL))))
#define UART0_STATE (*(((volatile uint32_t *)(UART0_ADDRESS + 4UL))))
#define UART0_CTRL (*(((volatile uint32_t *)(UART0_ADDRESS + 8UL))))
#define UART0_BAUDDIV (*(((volatile uint32_t *)(UART0_ADDRESS + 16UL))))

void UART_init(void)
{
    UART0_BAUDDIV = 16;
    UART0_CTRL = 1;
}

// Simplified function to output an integer as a string
void UART_print_int(int value)
{
    char buffer[12];                         // Buffer to store the integer as a string
    char *ptr = buffer + sizeof(buffer) - 1; // Start at the end of the buffer
    *ptr = '\0';                             // Null-terminate the string

    if (value == 0)
    {
        *(--ptr) = '0'; // Handle zero
    }
    else
    {
        bool is_negative = (value < 0);
        if (is_negative)
        {
            value = -value; // Make the value positive
        }

        // Convert the number to a string
        while (value > 0)
        {
            *(--ptr) = '0' + (value % 10);
            value /= 10;
        }

        if (is_negative)
        {
            *(--ptr) = '-'; // Add negative sign if needed
        }
    }

    // Send the string
    while (*ptr)
    {
        UART0_DATA = (unsigned int)(*ptr++);
    }
}

// UART_printf function with %d support
void UART_printf(const char *s, ...)
{
    va_list args;
    va_start(args, s);

    while (*s != '\0')
    {
        if (*s == '%')
        {
            s++; // Move to the character after '%'
            switch (*s)
            {
            case 'd':
            {
                int value = va_arg(args, int);
                UART_print_int(value);
                break;
            }
            case 's':
            {
                char *str = va_arg(args, char *);
                if (str == NULL)
                    str = "(null)";
                while (*str != '\0')
                {
                    UART0_DATA = (unsigned int)(*str++);
                }
                break;
            }
            case 'c':
            {
                // Note: char is promoted to int when passed through '...'
                int c = va_arg(args, int);
                UART0_DATA = (unsigned int)c;
                break;
            }
            case '%':
            {
                UART0_DATA = (unsigned int)'%';
                break;
            }
            default:
                // If we don't recognize it, print the % and the char literally
                UART0_DATA = (unsigned int)'%';
                UART0_DATA = (unsigned int)(*s);
                break;
            }
            s++; // Move past the specifier (d, s, or c)
        }
        else
        {
            UART0_DATA = (unsigned int)(*s);
            s++;
        }
    }

    va_end(args);
}