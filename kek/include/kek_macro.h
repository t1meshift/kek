#ifndef KEK_MACRO_H
#define KEK_MACRO_H

#define KEK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define KEK_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Expression form — only valid inside a function body. */
#define KEK_STATIC_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))

/* Declaration form, for file scope: layout invariants the code depends on.
   C99 has no _Static_assert, hence the negative-sized array trick. */
#define KEK_CONCAT_(a, b) a##b
#define KEK_CONCAT(a, b) KEK_CONCAT_(a, b)
#define KEK_STATIC_ASSERT_DECL(cond) \
    typedef char KEK_CONCAT(kek_static_assert_, __LINE__)[(cond) ? 1 : -1]

#define KEK_SWAP(type, a, b) do { \
    type kek_swap_temp_ = (a);    \
    (a) = (b);                    \
    (b) = kek_swap_temp_;         \
} while (0)


#endif