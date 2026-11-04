#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/// @brief Builds the save path for a ROM path without modifying the input.
///        The last extension is replaced with ".sav"; when the file name has no
///        extension, ".sav" is appended.
/// @param romPath The ROM path as passed by the launcher.
/// @param out Destination buffer.
/// @param outSize Size of the destination buffer in bytes.
/// @return false when the result (including the terminating NUL) does not fit.
static inline bool sav_buildSavePath(const char* romPath, char* out, size_t outSize)
{
    if (romPath == NULL || out == NULL || outSize < 5)
    {
        return false;
    }

    size_t length = strlen(romPath);
    size_t dot = length;
    for (size_t i = length; i > 0; --i)
    {
        char c = romPath[i - 1];
        if (c == '/' || c == '\\')
        {
            break;
        }
        if (c == '.')
        {
            dot = i - 1;
            break;
        }
    }

    // ".sav" plus the terminating NUL.
    if (dot + 4 >= outSize)
    {
        return false;
    }

    memcpy(out, romPath, dot);
    memcpy(out + dot, ".sav", 5);
    return true;
}
