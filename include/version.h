#pragma once
#include <stdio.h>
#include <stdlib.h>

// Основные параметры версии
#ifndef MARKETING_MAJOR
#define MARKETING_MAJOR 1
#endif

#ifndef MARKETING_MINOR
#define MARKETING_MINOR 2
#endif

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 100
#endif

#ifndef RC_NUMBER
#define RC_NUMBER 0
#endif

#ifndef VERSION_CODE
#define VERSION_CODE (MARKETING_MAJOR * 100000 + MARKETING_MINOR * 1000)
#endif

// Формирование строки версии
static inline void get_version_string(char *buf, size_t bufsize) {
    if (RC_NUMBER > 0) {
        snprintf(buf, bufsize, "v%d.%d (%d.%d-rc%d)",
            MARKETING_MAJOR, MARKETING_MINOR, VERSION_CODE, BUILD_NUMBER, RC_NUMBER);
    } else {
        snprintf(buf, bufsize, "v%d.%d (%d.%d)",
            MARKETING_MAJOR, MARKETING_MINOR, VERSION_CODE, BUILD_NUMBER);
    }
} 