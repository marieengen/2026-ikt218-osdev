#include <libc/stdio.h>
#include <libc/stdint.h>
#include <libc/stddef.h>
#include <libc/stdbool.h>
#include <libc/stdarg.h>
#include <terminal.h>

/*
 * putchar - write a single character to the terminal
 *
 * Returns the character written, or -1 on error (matching the C standard
 * signature even though in a bare-metal kernel we have no real error path).
 */
int putchar(int ic)
{
    char c = (char)ic;
    terminal_putchar(c);
    return ic;
}

/*
 * print - write exactly 'length' bytes from 'data' to the terminal
 *
 * Used internally by printf for string fragments.
 * Returns true on success (always, in this implementation).
 */
bool print(const char* data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        terminal_putchar(data[i]);
    }
    return true;
}

/* ------------------------------------------------------------------ *
 * Helpers for printf integer formatting                               *
 * ------------------------------------------------------------------ */

/*
 * print_uint - write an unsigned integer in the given base (2..16)
 */
static void print_uint(uint32_t value, uint32_t base)
{
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int  pos = 0;

    if (value == 0) {
        putchar('0');
        return;
    }

    /* Build the string in reverse */
    while (value != 0) {
        buf[pos++] = digits[value % base];
        value /= base;
    }

    /* Print in correct order */
    for (int i = pos - 1; i >= 0; i--) {
        putchar(buf[i]);
    }
}

/*
 * print_int - write a signed decimal integer
 */
static void print_int(int32_t value)
{
    if (value < 0) {
        putchar('-');
        /* Cast to uint32_t to handle INT32_MIN correctly */
        print_uint((uint32_t)(-(value + 1)) + 1, 10);
    } else {
        print_uint((uint32_t)value, 10);
    }
}

/*
 * printf - minimal format-string output
 *
 * Supported format specifiers:
 *   %c   - single character
 *   %s   - null-terminated string
 *   %d   - signed 32-bit decimal integer
 *   %u   - unsigned 32-bit decimal integer
 *   %x   - unsigned 32-bit hexadecimal (lower-case)
 *   %%   - literal '%'
 *
 * Returns 0 always (return value is not meaningful in this implementation).
 */
int printf(const char* __restrict__ format, ...)
{
    va_list args;
    va_start(args, format);

    for (size_t i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            putchar(format[i]);
            continue;
        }

        /* Move past the '%' */
        i++;
        switch (format[i]) {
            case 'c': {
                /* char is promoted to int when passed through '...' */
                char c = (char)va_arg(args, int);
                putchar(c);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (str == (void*)0) {
                    print("(null)", 6);
                } else {
                    terminal_write(str);
                }
                break;
            }
            case 'd': {
                int32_t val = (int32_t)va_arg(args, int);
                print_int(val);
                break;
            }
            case 'u': {
                uint32_t val = (uint32_t)va_arg(args, unsigned int);
                print_uint(val, 10);
                break;
            }
            case 'x': {
                uint32_t val = (uint32_t)va_arg(args, unsigned int);
                print_uint(val, 16);
                break;
            }
            case '%': {
                putchar('%');
                break;
            }
            default: {
                /* Unknown specifier: print it literally */
                putchar('%');
                putchar(format[i]);
                break;
            }
        }
    }

    va_end(args);
    return 0;
}
