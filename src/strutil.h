// strutil.h - tiny string helpers that behave the same under MSVC and MinGW.
//
// The plugin deliberately avoids the *_s "secure CRT" functions: MinGW-w64 does
// not declare them, and this project must build with either toolchain. It also
// avoids _stricmp, which lives in different headers per toolchain.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cwchar>

// Bounded wide-string copy that always NUL-terminates.
inline void WCopy(wchar_t* dst, size_t count, const wchar_t* src)
{
    if (!dst || count == 0)
        return;
    size_t i = 0;
    if (src) {
        for (; src[i] && i + 1 < count; ++i)
            dst[i] = src[i];
    }
    dst[i] = L'\0';
}

// Bounded wide-string append that always NUL-terminates.
inline void WAppend(wchar_t* dst, size_t count, const wchar_t* src)
{
    if (!dst || count == 0 || !src)
        return;
    size_t i = 0;
    while (i + 1 < count && dst[i])
        ++i;
    for (size_t j = 0; src[j] && i + 1 < count; ++j)
        dst[i++] = src[j];
    dst[i] = L'\0';
}

// Builds "<dir>\<file>".
inline void WJoinPath(wchar_t* dst, size_t count, const wchar_t* dir, const wchar_t* file)
{
    WCopy(dst, count, dir);
    WAppend(dst, count, L"\\");
    WAppend(dst, count, file);
}

// ASCII case-insensitive compare; `b` must be lower-case.
inline bool IEquals(const char* a, const char* b)
{
    if (!a || !b)
        return false;
    for (; *a && *b; ++a, ++b) {
        char c = *a;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != *b)
            return false;
    }
    return *a == '\0' && *b == '\0';
}

// vsnprintf that always NUL-terminates and returns the number of bytes written.
inline int VFormat(char* dst, size_t count, const char* fmt, va_list args)
{
    if (!dst || count == 0)
        return 0;
    const int n = ::vsnprintf(dst, count, fmt, args);
    if (n < 0) {
        dst[0] = '\0';
        return 0;
    }
    return (size_t)n >= count ? (int)(count - 1) : n;
}

// snprintf that always NUL-terminates and returns the number of bytes written.
inline int Format(char* dst, size_t count, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = VFormat(dst, count, fmt, args);
    va_end(args);
    return n;
}
