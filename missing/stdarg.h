/*
 * Replacement starg.h for Visual Studio's Clang/C2 - Public domain
 * See http://www.open-std.org/JTC1/sc22/wg14/www/docs/n1570.pdf section 7.16
 */

#ifndef STDARG_H
#define STDARG_H

typedef char* va_list;

#define va_arg(ap, type)	__builtin_va_arg(ap, type)
#define va_copy(dest, src)	(dest = src)
#define va_end(ap)			__builtin_va_end(ap)
#define va_start(ap, num)	__builtin_va_start(ap, num)

#endif
