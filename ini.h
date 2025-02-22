#ifndef INI_H
#define INI_H

/* inih -- simple .INI file parser
SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2009-2020, Ben Hoyt

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:
https://github.com/benhoyt/inih
*/

/* Make this header file easier to include in C++ code */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* Nonzero if ini_handler callback should accept lineno parameter. */
#ifndef INI_HANDLER_LINENO
    #define INI_HANDLER_LINENO 0
#endif

/* Visibility symbols, required for Windows DLLs */
#ifndef INI_API
    #if defined _WIN32 || defined __CYGWIN__
        #ifdef INI_SHARED_LIB
            #ifdef INI_SHARED_LIB_BUILDING
                #define INI_API __declspec(dllexport)
            #else
                #define INI_API __declspec(dllimport)
            #endif
        #else
            #define INI_API
        #endif
    #else
        #if defined(__GNUC__) && __GNUC__ >= 4
            #define INI_API __attribute__((visibility("default")))
        #else
            #define INI_API
        #endif
    #endif
#endif

/* Typedef for prototype of handler function. */
#if INI_HANDLER_LINENO
typedef int (*ini_handler)(void* user, const char* section, const char* name, const char* value, int lineno);
#else
typedef int (*ini_handler)(void* user, const char* section, const char* name, const char* value);
#endif

/* Typedef for prototype of fgets-style reader function. */
typedef char* (*ini_reader)(char* str, int num, void* stream);

/* Function declarations */
INI_API int ini_parse(const char* filename, ini_handler handler, void* user);
INI_API int ini_parse_file(FILE* file, ini_handler handler, void* user);
INI_API int ini_parse_stream(ini_reader reader, void* stream, ini_handler handler, void* user);
INI_API int ini_parse_string(const char* string, ini_handler handler, void* user);

/* Configuration macros */
#ifndef INI_ALLOW_MULTILINE
    #define INI_ALLOW_MULTILINE 1
#endif

#ifndef INI_ALLOW_BOM
    #define INI_ALLOW_BOM 1
#endif

#ifndef INI_START_COMMENT_PREFIXES
    #define INI_START_COMMENT_PREFIXES ";#"
#endif

#ifndef INI_ALLOW_INLINE_COMMENTS
    #define INI_ALLOW_INLINE_COMMENTS 1
#endif

#ifndef INI_INLINE_COMMENT_PREFIXES
    #define INI_INLINE_COMMENT_PREFIXES ";"
#endif

#ifndef INI_USE_STACK
    #define INI_USE_STACK 1
#endif

#ifndef INI_MAX_LINE
    #define INI_MAX_LINE 200
#endif

#ifndef INI_ALLOW_REALLOC
    #define INI_ALLOW_REALLOC 0
#endif

#ifndef INI_INITIAL_ALLOC
    #define INI_INITIAL_ALLOC 200
#endif

#ifndef INI_STOP_ON_FIRST_ERROR
    #define INI_STOP_ON_FIRST_ERROR 0
#endif

#ifndef INI_CALL_HANDLER_ON_NEW_SECTION
    #define INI_CALL_HANDLER_ON_NEW_SECTION 0
#endif

#ifndef INI_ALLOW_NO_VALUE
    #define INI_ALLOW_NO_VALUE 0
#endif

#ifndef INI_CUSTOM_ALLOCATOR
    #define INI_CUSTOM_ALLOCATOR 0
#endif

#ifdef __cplusplus
}
#endif

/* ======================================================================
   Header-only implementation (always included)
   ====================================================================== */

#undef INI_API
#define INI_API static inline

#include <ctype.h>
#include <string.h>
#if !INI_USE_STACK
    #if INI_CUSTOM_ALLOCATOR
        #include <stddef.h>
void* ini_malloc(size_t size);
void ini_free(void* ptr);
void* ini_realloc(void* ptr, size_t size);
    #else
        #include <stdlib.h>
        #define ini_malloc malloc
        #define ini_free free
        #define ini_realloc realloc
    #endif
#endif

#define MAX_SECTION 50
#define MAX_NAME 50

/* Used by ini_parse_string() to keep track of string parsing state. */
typedef struct {
    const char* ptr;
    size_t num_left;
} ini_parse_string_ctx;

/* Strip whitespace chars off end of given string, in place. Return s. */
static inline char* ini_rstrip(char* s) {
    char* p = s + strlen(s);
    while (p > s && isspace((unsigned char)(*--p))) *p = '\0';
    return s;
}

/* Return pointer to first non-whitespace char in given string. */
static inline char* ini_lskip(const char* s) {
    while (*s && isspace((unsigned char)(*s))) s++;
    return (char*)s;
}

/* Return pointer to first char (of chars) or inline comment in given string,
   or pointer to NUL at end of string if neither found. Inline comment must
   be prefixed by a whitespace character to register as a comment. */
static inline char* ini_find_chars_or_comment(const char* s, const char* chars) {
#if INI_ALLOW_INLINE_COMMENTS
    int was_space = 0;
    while (*s && (!chars || !strchr(chars, *s)) && !(was_space && strchr(INI_INLINE_COMMENT_PREFIXES, *s))) {
        was_space = isspace((unsigned char)(*s));
        s++;
    }
#else
    while (*s && (!chars || !strchr(chars, *s))) {
        s++;
    }
#endif
    return (char*)s;
}

/* Similar to strncpy, but ensures dest (size bytes) is NUL-terminated,
   and doesn't pad with NULs. */
static inline char* ini_strncpy0(char* dest, const char* src, size_t size) {
    size_t i;
    for (i = 0; i < size - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
    return dest;
}

/* Parse INI data from a stream using a reader function. */
INI_API int ini_parse_stream(ini_reader reader, void* stream, ini_handler handler, void* user) {
#if INI_USE_STACK
    char line[INI_MAX_LINE];
    size_t max_line = INI_MAX_LINE;
#else
    char* line;
    size_t max_line = INI_INITIAL_ALLOC;
#endif
#if INI_ALLOW_REALLOC && !INI_USE_STACK
    char* new_line;
    size_t offset;
#endif
    char section[MAX_SECTION] = "";
    char prev_name[MAX_NAME] = "";
    char* start;
    char* end;
    char* name;
    char* value;
    int lineno = 0;
    int error = 0;
#if !INI_USE_STACK
    line = (char*)ini_malloc(INI_INITIAL_ALLOC);
    if (!line) return -2;
#endif
#if INI_HANDLER_LINENO
    #define HANDLER(u, s, n, v) handler(u, s, n, v, lineno)
#else
    #define HANDLER(u, s, n, v) handler(u, s, n, v)
#endif
    while (reader(line, (int)max_line, stream) != NULL) {
#if INI_ALLOW_REALLOC && !INI_USE_STACK
        offset = strlen(line);
        while (offset == max_line - 1 && line[offset - 1] != '\n') {
            max_line *= 2;
            if (max_line > INI_MAX_LINE) max_line = INI_MAX_LINE;
            new_line = ini_realloc(line, max_line);
            if (!new_line) {
                ini_free(line);
                return -2;
            }
            line = new_line;
            if (reader(line + offset, (int)(max_line - offset), stream) == NULL) break;
            if (max_line >= INI_MAX_LINE) break;
            offset += strlen(line + offset);
        }
#endif
        lineno++;
        start = line;
#if INI_ALLOW_BOM
        if (lineno == 1 && (unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB &&
            (unsigned char)start[2] == 0xBF)
            start += 3;
#endif
        start = ini_lskip(ini_rstrip(start));
        if (strchr(INI_START_COMMENT_PREFIXES, *start)) {
            /* Comment line */
        }
#if INI_ALLOW_MULTILINE
        else if (*prev_name && *start && start > line) {
    #if INI_ALLOW_INLINE_COMMENTS
            end = ini_find_chars_or_comment(start, NULL);
            if (*end) *end = '\0';
            ini_rstrip(start);
    #endif
            if (!HANDLER(user, section, prev_name, start) && !error) error = lineno;
        }
#endif
        else if (*start == '[') {
            end = ini_find_chars_or_comment(start + 1, "]");
            if (*end == ']') {
                *end = '\0';
                ini_strncpy0(section, start + 1, sizeof(section));
                *prev_name = '\0';
#if INI_CALL_HANDLER_ON_NEW_SECTION
                if (!HANDLER(user, section, NULL, NULL) && !error) error = lineno;
#endif
            } else if (!error)
                error = lineno;
        } else if (*start) {
            end = ini_find_chars_or_comment(start, "=:");
            if (*end == '=' || *end == ':') {
                *end = '\0';
                name = ini_rstrip(start);
                value = end + 1;
#if INI_ALLOW_INLINE_COMMENTS
                end = ini_find_chars_or_comment(value, NULL);
                if (*end) *end = '\0';
#endif
                value = ini_lskip(value);
                ini_rstrip(value);
                ini_strncpy0(prev_name, name, sizeof(prev_name));
                if (!HANDLER(user, section, name, value) && !error) error = lineno;
            } else if (!error) {
#if INI_ALLOW_NO_VALUE
                *end = '\0';
                name = ini_rstrip(start);
                if (!HANDLER(user, section, name, NULL) && !error) error = lineno;
#else
                error = lineno;
#endif
            }
        }
#if INI_STOP_ON_FIRST_ERROR
        if (error) break;
#endif
    }
#if !INI_USE_STACK
    ini_free(line);
#endif
    return error;
}

/* Parse INI data from a FILE*. */
INI_API int ini_parse_file(FILE* file, ini_handler handler, void* user) {
    return ini_parse_stream((ini_reader)fgets, file, handler, user);
}

/* Parse INI data from a file by filename. */
INI_API int ini_parse(const char* filename, ini_handler handler, void* user) {
    FILE* file;
    int error;
    file = fopen(filename, "r");
    if (!file) return -1;
    error = ini_parse_file(file, handler, user);
    fclose(file);
    return error;
}

/* An ini_reader function to read the next line from a string buffer. */
static inline char* ini_reader_string(char* str, int num, void* stream) {
    ini_parse_string_ctx* ctx = (ini_parse_string_ctx*)stream;
    const char* ctx_ptr = ctx->ptr;
    size_t ctx_num_left = ctx->num_left;
    char* strp = str;
    char c;
    if (ctx_num_left == 0 || num < 2) return NULL;
    while (num > 1 && ctx_num_left != 0) {
        c = *ctx_ptr++;
        ctx_num_left--;
        *strp++ = c;
        if (c == '\n') break;
        num--;
    }
    *strp = '\0';
    ctx->ptr = ctx_ptr;
    ctx->num_left = ctx_num_left;
    return str;
}

/* Parse INI data from a zero-terminated string. */
INI_API int ini_parse_string(const char* string, ini_handler handler, void* user) {
    ini_parse_string_ctx ctx;
    ctx.ptr = string;
    ctx.num_left = strlen(string);
    return ini_parse_stream((ini_reader)ini_reader_string, &ctx, handler, user);
}

#endif /* INI_H */
