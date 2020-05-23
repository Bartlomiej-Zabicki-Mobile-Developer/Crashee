//
//  CrasheeLogger.h
//
//  Created by Karl Stenerud on 11-06-25.
//
//  Copyright (c) 2011 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//


/**
 * CrasheeLogger
 * ========
 *
 * Prints log entries to the console consisting of:
 * - Level (Error, Warn, Info, Debug, Trace)
 * - File
 * - Line
 * - Function
 * - Message
 *
 * Allows setting the minimum logging level in the preprocessor.
 *
 * Worcrashee in C or Objective-C contexts, with or without ARC, using CLANG or GCC.
 *
 *
 * =====
 * USAGE
 * =====
 *
 * Set the log level in your "Preprocessor Macros" build setting. You may choose
 * TRACE, DEBUG, INFO, WARN, ERROR. If nothing is set, it defaults to ERROR.
 *
 * Example: CrasheeLogger_Level=WARN
 *
 * Anything below the level specified for CrasheeLogger_Level will not be compiled
 * or printed.
 * 
 *
 * Next, include the header file:
 *
 * #include "CrasheeLogger.h"
 *
 *
 * Next, call the logger functions from your code (using objective-c strings
 * in objective-C files and regular strings in regular C files):
 *
 * Code:
 *    CrasheeLOG_ERROR(@"Some error message");
 *
 * Prints:
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message 
 *
 * Code:
 *    CrasheeLOG_INFO(@"Info about %@", someObject);
 *
 * Prints:
 *    2011-07-16 05:44:05.239 TestApp[4473:f803] INFO : SomeClass.m (20): -[SomeFunction]: Info about <NSObject: 0xb622840>
 *
 *
 * The "BASIC" versions of the macros behave exactly like NSLog() or printf(),
 * except they respect the CrasheeLogger_Level setting:
 *
 * Code:
 *    CrasheeLOGBASIC_ERROR(@"A basic log entry");
 *
 * Prints:
 *    2011-07-16 05:44:05.916 TestApp[4473:f803] A basic log entry
 *
 *
 * NOTE: In C files, use "" instead of @"" in the format field. Logging calls
 *       in C files do not print the NSLog preamble:
 *
 * Objective-C version:
 *    CrasheeLOG_ERROR(@"Some error message");
 *
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message
 *
 * C version:
 *    CrasheeLOG_ERROR("Some error message");
 *
 *    ERROR: SomeClass.c (21): SomeFunction(): Some error message
 *
 *
 * =============
 * LOCAL LOGGING
 * =============
 *
 * You can control logging messages at the local file level using the
 * "CrasheeLogger_LocalLevel" define. Note that it must be defined BEFORE
 * including CrasheeLogger.h
 *
 * The CrasheeLOG_XX() and CrasheeLOGBASIC_XX() macros will print out based on the LOWER
 * of CrasheeLogger_Level and CrasheeLogger_LocalLevel, so if CrasheeLogger_Level is DEBUG
 * and CrasheeLogger_LocalLevel is TRACE, it will print all the way down to the trace
 * level for the local file where CrasheeLogger_LocalLevel was defined, and to the
 * debug level everywhere else.
 *
 * Example:
 *
 * // CrasheeLogger_LocalLevel, if defined, MUST come BEFORE including CrasheeLogger.h
 * #define CrasheeLogger_LocalLevel TRACE
 * #import "CrasheeLogger.h"
 *
 *
 * ===============
 * IMPORTANT NOTES
 * ===============
 *
 * The C logger changes its behavior depending on the value of the preprocessor
 * define CrasheeLogger_CBufferSize.
 *
 * If CrasheeLogger_CBufferSize is > 0, the C logger will behave in an async-safe
 * manner, calling write() instead of printf(). Any log messages that exceed the
 * length specified by CrasheeLogger_CBufferSize will be truncated.
 *
 * If CrasheeLogger_CBufferSize == 0, the C logger will use printf(), and there will
 * be no limit on the log message length.
 *
 * CrasheeLogger_CBufferSize can only be set as a preprocessor define, and will
 * default to 1024 if not specified during compilation.
 */


// ============================================================================
#pragma mark - (internal) -
// ============================================================================


#ifndef HDR_CrasheeLogger_h
#define HDR_CrasheeLogger_h

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>


#ifdef __OBJC__

#import <CoreFoundation/CoreFoundation.h>

void i_crasheelog_logObjC(const char* level,
                     const char* file,
                     int line,
                     const char* function,
                     CFStringRef fmt, ...);

void i_crasheelog_logObjCBasic(CFStringRef fmt, ...);

#define i_CrasheeLOG_FULL(LEVEL,FILE,LINE,FUNCTION,FMT,...) i_crasheelog_logObjC(LEVEL,FILE,LINE,FUNCTION,(__bridge CFStringRef)FMT,##__VA_ARGS__)
#define i_CrasheeLOG_BASIC(FMT, ...) i_crasheelog_logObjCBasic((__bridge CFStringRef)FMT,##__VA_ARGS__)

#else // __OBJC__

void i_crasheelog_logC(const char* level,
                  const char* file,
                  int line,
                  const char* function,
                  const char* fmt, ...);

void i_crasheelog_logCBasic(const char* fmt, ...);

#define i_CrasheeLOG_FULL i_crasheelog_logC
#define i_CrasheeLOG_BASIC i_crasheelog_logCBasic

#endif // __OBJC__


/* Back up any existing defines by the same name */
#ifdef Crashee_NONE
    #define CrasheeLOG_BAK_NONE Crashee_NONE
    #undef Crashee_NONE
#endif
#ifdef ERROR
    #define CrasheeLOG_BAK_ERROR ERROR
    #undef ERROR
#endif
#ifdef WARN
    #define CrasheeLOG_BAK_WARN WARN
    #undef WARN
#endif
#ifdef INFO
    #define CrasheeLOG_BAK_INFO INFO
    #undef INFO
#endif
#ifdef DEBUG
    #define CrasheeLOG_BAK_DEBUG DEBUG
    #undef DEBUG
#endif
#ifdef TRACE
    #define CrasheeLOG_BAK_TRACE TRACE
    #undef TRACE
#endif


#define CrasheeLogger_Level_None   0
#define CrasheeLogger_Level_Error 10
#define CrasheeLogger_Level_Warn  20
#define CrasheeLogger_Level_Info  30
#define CrasheeLogger_Level_Debug 40
#define CrasheeLogger_Level_Trace 50

#define Crashee_NONE  CrasheeLogger_Level_None
#define ERROR CrasheeLogger_Level_Error
#define WARN  CrasheeLogger_Level_Warn
#define INFO  CrasheeLogger_Level_Info
#define DEBUG CrasheeLogger_Level_Debug
#define TRACE CrasheeLogger_Level_Trace


#ifndef CrasheeLogger_Level
    #define CrasheeLogger_Level CrasheeLogger_Level_Error
#endif

#ifndef CrasheeLogger_LocalLevel
    #define CrasheeLogger_LocalLevel CrasheeLogger_Level_None
#endif

#define a_CrasheeLOG_FULL(LEVEL, FMT, ...) \
    i_CrasheeLOG_FULL(LEVEL, \
                 __FILE__, \
                 __LINE__, \
                 __PRETTY_FUNCTION__, \
                 FMT, \
                 ##__VA_ARGS__)



// ============================================================================
#pragma mark - API -
// ============================================================================

/** Set the filename to log to.
 *
 * @param filename The file to write to (NULL = write to stdout).
 *
 * @param overwrite If true, overwrite the log file.
 */
bool crasheelog_setLogFilename(const char* filename, bool overwrite);

/** Clear the log file. */
bool crasheelog_clearLogFile(void);

/** Tests if the logger would print at the specified level.
 *
 * @param LEVEL The level to test for. One of:
 *            CrasheeLogger_Level_Error,
 *            CrasheeLogger_Level_Warn,
 *            CrasheeLogger_Level_Info,
 *            CrasheeLogger_Level_Debug,
 *            CrasheeLogger_Level_Trace,
 *
 * @return TRUE if the logger would print at the specified level.
 */
#define CrasheeLOG_PRINTS_AT_LEVEL(LEVEL) \
    (CrasheeLogger_Level >= LEVEL || CrasheeLogger_LocalLevel >= LEVEL)

/** Log a message regardless of the log settings.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#define CrasheeLOG_ALWAYS(FMT, ...) a_CrasheeLOG_FULL("FORCE", FMT, ##__VA_ARGS__)
#define CrasheeLOGBASIC_ALWAYS(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)


/** Log an error.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if CrasheeLOG_PRINTS_AT_LEVEL(CrasheeLogger_Level_Error)
    #define CrasheeLOG_ERROR(FMT, ...) a_CrasheeLOG_FULL("ERROR", FMT, ##__VA_ARGS__)
    #define CrasheeLOGBASIC_ERROR(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define CrasheeLOG_ERROR(FMT, ...)
    #define CrasheeLOGBASIC_ERROR(FMT, ...)
#endif

/** Log a warning.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if CrasheeLOG_PRINTS_AT_LEVEL(CrasheeLogger_Level_Warn)
    #define CrasheeLOG_WARN(FMT, ...)  a_CrasheeLOG_FULL("WARN ", FMT, ##__VA_ARGS__)
    #define CrasheeLOGBASIC_WARN(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define CrasheeLOG_WARN(FMT, ...)
    #define CrasheeLOGBASIC_WARN(FMT, ...)
#endif

/** Log an info message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if CrasheeLOG_PRINTS_AT_LEVEL(CrasheeLogger_Level_Info)
    #define CrasheeLOG_INFO(FMT, ...)  a_CrasheeLOG_FULL("INFO ", FMT, ##__VA_ARGS__)
    #define CrasheeLOGBASIC_INFO(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define CrasheeLOG_INFO(FMT, ...)
    #define CrasheeLOGBASIC_INFO(FMT, ...)
#endif

/** Log a debug message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if CrasheeLOG_PRINTS_AT_LEVEL(CrasheeLogger_Level_Debug)
    #define CrasheeLOG_DEBUG(FMT, ...) a_CrasheeLOG_FULL("DEBUG", FMT, ##__VA_ARGS__)
    #define CrasheeLOGBASIC_DEBUG(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define CrasheeLOG_DEBUG(FMT, ...)
    #define CrasheeLOGBASIC_DEBUG(FMT, ...)
#endif

/** Log a trace message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if CrasheeLOG_PRINTS_AT_LEVEL(CrasheeLogger_Level_Trace)
    #define CrasheeLOG_TRACE(FMT, ...) a_CrasheeLOG_FULL("TRACE", FMT, ##__VA_ARGS__)
    #define CrasheeLOGBASIC_TRACE(FMT, ...) i_CrasheeLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define CrasheeLOG_TRACE(FMT, ...)
    #define CrasheeLOGBASIC_TRACE(FMT, ...)
#endif



// ============================================================================
#pragma mark - (internal) -
// ============================================================================

/* Put everything back to the way we found it. */
#undef ERROR
#ifdef CrasheeLOG_BAK_ERROR
    #define ERROR CrasheeLOG_BAK_ERROR
    #undef CrasheeLOG_BAK_ERROR
#endif
#undef WARNING
#ifdef CrasheeLOG_BAK_WARN
    #define WARNING CrasheeLOG_BAK_WARN
    #undef CrasheeLOG_BAK_WARN
#endif
#undef INFO
#ifdef CrasheeLOG_BAK_INFO
    #define INFO CrasheeLOG_BAK_INFO
    #undef CrasheeLOG_BAK_INFO
#endif
#undef DEBUG
#ifdef CrasheeLOG_BAK_DEBUG
    #define DEBUG CrasheeLOG_BAK_DEBUG
    #undef CrasheeLOG_BAK_DEBUG
#endif
#undef TRACE
#ifdef CrasheeLOG_BAK_TRACE
    #define TRACE CrasheeLOG_BAK_TRACE
    #undef CrasheeLOG_BAK_TRACE
#endif


#ifdef __cplusplus
}
#endif

#endif // HDR_CrasheeLogger_h
