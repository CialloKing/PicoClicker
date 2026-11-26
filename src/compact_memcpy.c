#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    // Volatile stores keep the compiler from replacing the loop with a recursive memcpy call.
    volatile unsigned char *output = destination;
    const unsigned char *input = source;
    for (size_t i = 0; i < count; ++i)
    {
        output[i] = input[i];
    }
    return destination;
}
