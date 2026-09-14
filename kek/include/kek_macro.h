#ifndef KEK_MACRO_H
#define KEK_MACRO_H

#define KEK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define KEK_MAX(a, b) ((a) > (b) ? (a) : (b))
#define KEK_STATIC_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))
#define KEK_SWAP(type, a, b) do { \
type _kek_temp_ = a;              \
a = b;                            \
b = _kek_temp_;                   \
} while (0)


#endif