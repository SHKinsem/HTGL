#ifndef CTEST_UTIL_H
#define CTEST_UTIL_H
#include <stdio.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

#endif
