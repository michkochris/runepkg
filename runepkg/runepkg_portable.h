/******************************************************************************
 * Filename:    runepkg_portable.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Portability layer for C89/ANSI C compatibility
 *
 * This header provides standard types and definitions that were not part of
 * the original ISO C90 (C89) standard but are required by the codebase.
 ******************************************************************************/

#ifndef RUNEPKG_PORTABLE_H
#define RUNEPKG_PORTABLE_H

/* Detect C99 or later */
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__GNUC__) || defined(__clang__)
/* Modern compiler or GCC/Clang extension support */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#else
/* Strict C89/C90 */
#include <stddef.h>

#ifndef __cplusplus
/* Boolean type for C89 */
typedef int bool;
#define true 1
#define false 0
#endif

/* Fixed-width integers for C89 (common 32/64 bit platforms) */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;

#ifdef _MSC_VER
typedef unsigned __int64   uint64_t;
typedef __int64            int64_t;
#else
/* Note: long long is a C99 feature, but supported by almost all C89 compilers */
typedef unsigned long long uint64_t;
typedef long long          int64_t;
#endif

#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#endif /* RUNEPKG_PORTABLE_H */
