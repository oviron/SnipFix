/* SnipFix links without the C runtime. Compilers may still emit calls to
 * memset/memcpy for struct initialisation and copies, so provide them. */
#include <stddef.h>

#ifdef _MSC_VER
#include <intrin.h>
#pragma function(memset)
#pragma function(memcpy)
void *memset(void *dst, int c, size_t n) { __stosb((unsigned char *)dst, (unsigned char)c, n); return dst; }
void *memcpy(void *dst, const void *src, size_t n) { __movsb((unsigned char *)dst, (const unsigned char *)src, n); return dst; }
#else
/* rep movs/stos keep the compiler from turning these loops back into memset/memcpy calls */
void *memset(void *dst, int c, size_t n)
{
    void *d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n)
{
    void *d = dst;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
    return dst;
}
#endif
