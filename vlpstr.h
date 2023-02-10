#ifndef VSZ_VLPSTR_H
#define VSZ_VLPSTR_H

/**
 * vlpstr - a barebones header-only library for length-prefixed strings
 *
 * Copyright 2023 James Arcus <jimbo@ucc.asn.au>
 * Released under the terms of the MIT license.
 *
 * vlpstr.h provides a struct whose purpose is to store string data using a
 * varialbe-size length prefix rather than a null terminator as the main
 * method of controlling string length. This is conceptually similar to
 * "Pascal-style" strings, though with an attempt to reduce overhead in the
 * simple case while maintaining flexibility.
 *
 * The library makes a few key design decisions:
 * - A variable-length size prefix allows for no overhead compared to C
 *   strings for small (< 128 byte) lengths, while allowing for large strings
 *   to be addressed by up to 64 bits.
 * - The provided routines are agnostic to storage duration or allocation
 *   method, unlike many string libraries which bake allocation and growth
 *   into the core implmentation for ease-of-use. The aim for `vlpstr` was to
 *   be as conceptually similar to C strings as possible, in order to be a
 *   viable replacement in (almost) all circumstances (including as stack
 *   variables, and in APIs).
 * - The structure itself is agnostic to null-termination. String data *may*
 *   be followed by a null terminator, though it should not be included in the
 *   string length. The provided `vlpstr_import` routine preserves the null
 *   terminator in this way, and `vlpstr_rsize` assumes its caller will want
 *   to reserve sufficient space for one. This allows for direct
 *   interoperation via `vlpstr_data` with any API expecting to receive a C
 *   string, though it does break the "zero overhead for small strings" goal
 *   by 1 byte. Conveniently, the empty string can be represented as just a
 *   single '0' flag byte, identical to an empty C string.
 * - Flexibility is key. Redundant/"overlong" encodings of string length are
 *   allowed, and can be useful when e.g. truncating a long string without
 *   shifting the remaining string data in memory. The only requirement is
 *   the `flags` value match the layout. `flags = 0x0a` is a valid string of
 *   length 10 whose first character is in `data[0]`; conversely,
 *   `flags = 0x87, data[] = {10, 0, 0, 0, 0, 0, 0, ...}` is also a valid
 *   string of length 10, with a 7-byte length encoding and whose first string
 *   data byte is `data[7]`.
 */


#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
    /**
     * Example memory layouts
     *
     *  Simple:
     *      /-----------------------------------------------\
     *      |  6  | 'H' | 'e' | 'l' | 'l' | 'o' | '!' |  0  |
     *      \-----------------------------------------------/
     *
     *  Multibyte (e.g. with 2-byte length encoding):
     *      /-----------------------------------------------\
     *      | 82h | 51h | 03h | 'A' | 'c' | 'c' | 'o' | 'r' |
     *      |-----+-----+-----+-----+-----+-----+-----+-----|
     *      | 'd' | 'i' | 'n' | 'g' | ' ' | 't' | 'o' | ' ' |
     *      |-----+-----+-----+-----+-----+-----+-----+-----|
     *      | 'a' | 'l' | 'l' | ' ' | 'k' | 'n' | 'o' | 'w' |
     *      |-----+-----+-----+-----+-----+-----+-----+-----|
     *      | 'n' | ' ' | 'l' | 'a' | 'w' | 's' | ' ' | 'o' |
     *      |-----+-----+-----+-----+-----+-----+-----+-----|
     *      |                      ...                      |
     *      |-----+-----+-----+-----+-----+-----------------/
     *      | 'u' | 's' | 'e' | '!' |  0  |
     *      \-----------------------------/
     */

    // Flags: If the string length is < 128 then the string is simple,
    // `flags[7]` is 0 and `flags[0..6]` is the string length. Otherwise,
    // `flags[7]` is 1 signifying the "multibyte length" follows,
    // `flags[4..6]` is reserved for future use and must be set to 0, and
    // `flags[0..3]` is the number of bytes in the following string length.
    uint8_t flags;

    // If the string is simple (`flags & 0x80 == 0`), then the string data
    // follows immediately after `flags`, beginning at `data[0]`. If the
    // string is "multibyte length", then the first `flags[0..3]`
    // bytes of `data` is the string length (stored in little-endian order),
    // and the string data commnces immediately afterwards, at
    // `data[flags[0..3]]`.
    uint8_t data[]; // FAM syntax allows overlaying on arbitrary allocations

} vlpstr;


// Copy data from a C string into the vlpstr pointed to by `vlps`.
// Automatically determines the correct accounting flags and length bytes. Not
// needed for copying from vlpstr to vlpstr, that can be done with a simple
// call to `memcpy`.
static inline void vlpstr_import(vlpstr* vlps, const char* zs)
{
    size_t len = strlen(zs);

    if (len < 128) {
        // Flags are just the length in the simple case
        vlps->flags = len;
        // Store the string, including the null byte
        memcpy(vlps->data, zs, len + 1);
    }
    else {
        // Calculate space required to store the length, rounded up to the
        // nearest byte
        unsigned nr_len_bytes = (64 - __builtin_clzll(len) + 7) / 8;
        // Store multibyte flag and number of length bytes
        vlps->flags = nr_len_bytes | 0x80;
        // Store the string length in the length bytes
        size_t len_tmp = len;
        for (unsigned i = 0; i < nr_len_bytes; ++i) {
            vlps->data[i] = len_tmp;
            len_tmp >>= 8;
        }
        // Finally, store the string data after the length bytes, including
        // the null byte
        memcpy(&vlps->data[nr_len_bytes], zs, len + 1);
    }
}

// Retrieve a pointer to the start of string data (a C string).
static inline uint8_t* vlpstr_data(vlpstr* vlps)
{
    if (vlps->flags < 0x80) {
        return vlps->data;
    }
    else {
        unsigned nr_len_bytes = vlps->flags & 0x0f;
        return &vlps->data[nr_len_bytes];
    }
}

// Calculate the string length of the data stored in `vlps`. Does not include
// accouning overhead (flags, length, null).
static inline size_t vlpstr_len(const vlpstr* vlps)
{
    if (vlps->flags < 0x80) {
        return vlps->flags;
    }
    else {
        unsigned nr_len_bytes = vlps->flags & 0x0f;
        size_t len = 0;
        for (unsigned i = 0; i < nr_len_bytes; ++i) {
            len |= (size_t) vlps->data[i] << (i * 8);
        }
        return len;
    }
}

// Helper function to calculate the space required to be allocated to store a
// vlpstr containing a C string of length `len`.
static inline size_t vlpstr_rsize(size_t len)
{
    if (len < 0x80) {
        // Need space for flags + null
        return len + 2;
    }
    else {
        // Need space for length bytes, flags, and null byte
        return len + (64 - __builtin_clzll(len) + 7) / 8 + 2;
    }
}

#endif // VSZ_VLPSTR_H
