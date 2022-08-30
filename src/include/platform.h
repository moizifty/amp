#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_WIN32 0
#define PLATFORM_WIN64 1
#define PLATFORM_UNIX 2

#if defined(_WIN64)
    #define PLATFORM    PLATFORM_WIN64
#elif defined(_WIN32)
    #define PLATFORM    PLATFORM_WIN32
#elif defined(_unix) || defined(_unix_) || defined(unix)
    #define PLATFORM    PLATFORM_UNIX
#endif

typedef enum PlatformKind PlatformKind;

#endif
