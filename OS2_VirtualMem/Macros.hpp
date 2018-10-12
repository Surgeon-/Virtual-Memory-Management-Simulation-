#pragma once

// Output:
#define CPRINT

#ifdef  CPRINT

extern bool __do_print;
#define PRINT_INIT(val) bool __do_print = (val);
#define PRINT(text) do { if (__do_print) std::cout << text; } while (false)
#define PRINTLN(text) do { if (__do_print) std::cout << text << "\n"; } while (false)
#define PRINT_SET_ACTIVE(val) __do_print = (val)

#else

#define PRINT_INIT(val)
#define PRINT(text)
#define PRINTLN(text)
#define PRINT_SET_ACTIVE(val)

#endif

// Math:
#define MAX(a, b) ( (a > b) ? (a) : (b) )
#define MIN(a, b) ( (a < b) ? (a) : (b) )
#define CLAMP(val, min, max) MIN(max, MAX(min, val))
#define SIGN(val) ((val > 0) - (val < 0))
#define NOT(val)  (!(val))

#define DIV_CEIL(n, divisor) (1 + ((n) - 1) / (divisor)) 

// Bitwise:
#define BIT_SET(number, bit)    (number |  (1 << bit))
#define BIT_CLR(number, bit)    (number & ~(1 << bit))
#define BIT_TGL(number, bit)    (number ^  (1 << bit))
#define BIT_GET(number, bit)   ((number &  (1 << bit)) != 0)

#define BIT_VAL(number, bit, value) ((number & ~(1 << bit)) | (value << bit))

// Other:
#define HALT(message) do { std::cout << message << "\n" <<\
         "Press Enter to exit."; /*throw std::exception();*/ getchar(); std::exit(1); } while (false)

