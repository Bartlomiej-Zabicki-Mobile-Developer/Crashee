//
//  CrasheeCrashReport.m
//
//  Created by Karl Stenerud on 2012-01-28.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
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


#include "CrasheeCrashReport.h"

#include "CrasheeCrashReportFields.h"
#include "CrasheeCrashReportWriter.h"
#include "Tools/CrasheeDynamicLinker.h"
#include "Tools/CrasheeFileUtils.h"
#include "Tools/CrasheeJSONCodec.h"
#include "Tools/CrasheeCPU.h"
#include "Tools/CrasheeMemory.h"
#include "Tools/CrasheeMach.h"
#include "Tools/CrasheeThread.h"
#include "Tools/CrasheeObjC.h"
#include "Tools/CrasheeSignalInfo.h"
#include "Tools/CrasheeString.h"
#include "CrasheeCrashReportVersion.h"
#include "Tools/CrasheeStackCursor_Backtrace.h"
#include "Tools/CrasheeStackCursor_MachineContext.h"
#include "CrasheeSystemCapabilities.h"
#include "CrasheeCrashCachedData.h"

//#define CrasheeLogger_LocalLevel TRACE
#include "Tools/CrasheeLogger.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// ============================================================================
#pragma mark - Constants -
// ============================================================================

/** Default number of objects, subobjects, and ivars to record from a memory loc */
#define kDefaultMemorySearchDepth 15

/** How far to search the stack (in pointer sized jumps) for notable data. */
#define kStackNotableSearchBackDistance 20
#define kStackNotableSearchForwardDistance 10

/** How much of the stack to dump (in pointer sized jumps). */
#define kStackContentsPushedDistance 20
#define kStackContentsPoppedDistance 10
#define kStackContentsTotalDistance (kStackContentsPushedDistance + kStackContentsPoppedDistance)

/** The minimum length for a valid string. */
#define kMinStringLength 4


// ============================================================================
#pragma mark - JSON Encoding -
// ============================================================================

#define getJsonContext(REPORT_WRITER) ((CrasheeJSONEncodeContext*)((REPORT_WRITER)->context))

/** Used for writing hex string values. */
static const char g_hexNybbles[] =
{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

// ============================================================================
#pragma mark - Runtime Config -
// ============================================================================

typedef struct
{
    /** If YES, introspect memory contents during a crash.
     * Any Objective-C objects or C strings near the stack pointer or referenced by
     * cpu registers or exceptions will be recorded in the crash report, along with
     * their contents.
     */
    bool enabled;
    
    /** List of classes that should never be introspected.
     * Whenever a class in this list is encountered, only the class name will be recorded.
     */
    const char** restrictedClasses;
    int restrictedClassesCount;
} CrasheeCrash_IntrospectionRules;

static const char* g_userInfoJSON;
static CrasheeCrash_IntrospectionRules g_introspectionRules;
static CrasheeReportWriteCallback g_userSectionWriteCallback;


#pragma mark Callbaccrashee

static void addBooleanElement(const CrasheeCrashReportWriter* const writer, const char* const key, const bool value)
{
    crasheejson_addBooleanElement(getJsonContext(writer), key, value);
}

static void addFloatingPointElement(const CrasheeCrashReportWriter* const writer, const char* const key, const double value)
{
    crasheejson_addFloatingPointElement(getJsonContext(writer), key, value);
}

static void addIntegerElement(const CrasheeCrashReportWriter* const writer, const char* const key, const int64_t value)
{
    crasheejson_addIntegerElement(getJsonContext(writer), key, value);
}

static void addUIntegerElement(const CrasheeCrashReportWriter* const writer, const char* const key, const uint64_t value)
{
    crasheejson_addUIntegerElement(getJsonContext(writer), key, value);
}

static void addStringElement(const CrasheeCrashReportWriter* const writer, const char* const key, const char* const value)
{
    crasheejson_addStringElement(getJsonContext(writer), key, value, CrasheeJSON_SIZE_AUTOMATIC);
}

static void addTextFileElement(const CrasheeCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    const int fd = open(filePath, O_RDONLY);
    if(fd < 0)
    {
        CrasheeLOG_ERROR("Could not open file %s: %s", filePath, strerror(errno));
        return;
    }

    if(crasheejson_beginStringElement(getJsonContext(writer), key) != CrasheeJSON_OK)
    {
        CrasheeLOG_ERROR("Could not start string element");
        goto done;
    }

    char buffer[512];
    int bytesRead;
    for(bytesRead = (int)read(fd, buffer, sizeof(buffer));
        bytesRead > 0;
        bytesRead = (int)read(fd, buffer, sizeof(buffer)))
    {
        if(crasheejson_appendStringElement(getJsonContext(writer), buffer, bytesRead) != CrasheeJSON_OK)
        {
            CrasheeLOG_ERROR("Could not append string element");
            goto done;
        }
    }

done:
    crasheejson_endStringElement(getJsonContext(writer));
    close(fd);
}

static void addDataElement(const CrasheeCrashReportWriter* const writer,
                           const char* const key,
                           const char* const value,
                           const int length)
{
    crasheejson_addDataElement(getJsonContext(writer), key, value, length);
}

static void beginDataElement(const CrasheeCrashReportWriter* const writer, const char* const key)
{
    crasheejson_beginDataElement(getJsonContext(writer), key);
}

static void appendDataElement(const CrasheeCrashReportWriter* const writer, const char* const value, const int length)
{
    crasheejson_appendDataElement(getJsonContext(writer), value, length);
}

static void endDataElement(const CrasheeCrashReportWriter* const writer)
{
    crasheejson_endDataElement(getJsonContext(writer));
}

static void addUUIDElement(const CrasheeCrashReportWriter* const writer, const char* const key, const unsigned char* const value)
{
    if(value == NULL)
    {
        crasheejson_addNullElement(getJsonContext(writer), key);
    }
    else
    {
        char uuidBuffer[37];
        const unsigned char* src = value;
        char* dst = uuidBuffer;
        for(int i = 0; i < 4; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 6; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }

        crasheejson_addStringElement(getJsonContext(writer), key, uuidBuffer, (int)(dst - uuidBuffer));
    }
}

static void addJSONElement(const CrasheeCrashReportWriter* const writer,
                           const char* const key,
                           const char* const jsonElement,
                           bool closeLastContainer)
{
    int jsonResult = crasheejson_addJSONElement(getJsonContext(writer),
                                           key,
                                           jsonElement,
                                           (int)strlen(jsonElement),
                                           closeLastContainer);
    if(jsonResult != CrasheeJSON_OK)
    {
        char errorBuff[100];
        snprintf(errorBuff,
                 sizeof(errorBuff),
                 "Invalid JSON data: %s",
                 crasheejson_stringForError(jsonResult));
        crasheejson_beginObject(getJsonContext(writer), key);
        crasheejson_addStringElement(getJsonContext(writer),
                                CrasheeCrashField_Error,
                                errorBuff,
                                CrasheeJSON_SIZE_AUTOMATIC);
        crasheejson_addStringElement(getJsonContext(writer),
                                CrasheeCrashField_JSONData,
                                jsonElement,
                                CrasheeJSON_SIZE_AUTOMATIC);
        crasheejson_endContainer(getJsonContext(writer));
    }
}

static void addJSONElementFromFile(const CrasheeCrashReportWriter* const writer,
                                   const char* const key,
                                   const char* const filePath,
                                   bool closeLastContainer)
{
    crasheejson_addJSONFromFile(getJsonContext(writer), key, filePath, closeLastContainer);
}

static void beginObject(const CrasheeCrashReportWriter* const writer, const char* const key)
{
    crasheejson_beginObject(getJsonContext(writer), key);
}

static void beginArray(const CrasheeCrashReportWriter* const writer, const char* const key)
{
    crasheejson_beginArray(getJsonContext(writer), key);
}

static void endContainer(const CrasheeCrashReportWriter* const writer)
{
    crasheejson_endContainer(getJsonContext(writer));
}


static void addTextLinesFromFile(const CrasheeCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    char readBuffer[1024];
    CrasheeBufferedReader reader;
    if(!crasheefu_openBufferedReader(&reader, filePath, readBuffer, sizeof(readBuffer)))
    {
        return;
    }
    char buffer[1024];
    beginArray(writer, key);
    {
        for(;;)
        {
            int length = sizeof(buffer);
            crasheefu_readBufferedReaderUntilChar(&reader, '\n', buffer, &length);
            if(length <= 0)
            {
                break;
            }
            buffer[length - 1] = '\0';
            crasheejson_addStringElement(getJsonContext(writer), NULL, buffer, CrasheeJSON_SIZE_AUTOMATIC);
        }
    }
    endContainer(writer);
    crasheefu_closeBufferedReader(&reader);
}

static int addJSONData(const char* restrict const data, const int length, void* restrict userData)
{
    CrasheeBufferedWriter* writer = (CrasheeBufferedWriter*)userData;
    const bool success = crasheefu_writeBufferedWriter(writer, data, length);
    return success ? CrasheeJSON_OK : CrasheeJSON_ERROR_CANNOT_ADD_DATA;
}


// ============================================================================
#pragma mark - Utility -
// ============================================================================

/** Check if a memory address points to a valid null terminated UTF-8 string.
 *
 * @param address The address to check.
 *
 * @return true if the address points to a string.
 */
static bool isValidString(const void* const address)
{
    if((void*)address == NULL)
    {
        return false;
    }

    char buffer[500];
    if((uintptr_t)address+sizeof(buffer) < (uintptr_t)address)
    {
        // Wrapped around the address range.
        return false;
    }
    if(!crasheemem_copySafely(address, buffer, sizeof(buffer)))
    {
        return false;
    }
    return crasheestring_isNullTerminatedUTF8String(buffer, kMinStringLength, sizeof(buffer));
}

/** Get the backtrace for the specified machine context.
 *
 * This function will choose how to fetch the backtrace based on the crash and
 * machine context. It may store the backtrace in backtraceBuffer unless it can
 * be fetched directly from memory. Do not count on backtraceBuffer containing
 * anything. Always use the return value.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The machine context.
 *
 * @param cursor The stack cursor to fill.
 *
 * @return True if the cursor was filled.
 */
static bool getStackCursor(const CrasheeCrash_MonitorContext* const crash,
                           const struct CrasheeMachineContext* const machineContext,
                           CrasheeStackCursor *cursor)
{
    if(crasheemc_getThreadFromContext(machineContext) == crasheemc_getThreadFromContext(crash->offendingMachineContext))
    {
        *cursor = *((CrasheeStackCursor*)crash->stackCursor);
        return true;
    }

    crasheesc_initWithMachineContext(cursor, CrasheeSC_STACK_OVERFLOW_THRESHOLD, machineContext);
    return true;
}


// ============================================================================
#pragma mark - Report Writing -
// ============================================================================

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const CrasheeCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit);

/** Write a string to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNSStringContents(const CrasheeCrashReportWriter* const writer,
                                  const char* const key,
                                  const uintptr_t objectAddress,
                                  __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(crasheeobjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a URL to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeURLContents(const CrasheeCrashReportWriter* const writer,
                             const char* const key,
                             const uintptr_t objectAddress,
                             __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(crasheeobjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a date to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeDateContents(const CrasheeCrashReportWriter* const writer,
                              const char* const key,
                              const uintptr_t objectAddress,
                              __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, crasheeobjc_dateContents(object));
}

/** Write a number to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNumberContents(const CrasheeCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t objectAddress,
                                __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, crasheeobjc_numberAsFloat(object));
}

/** Write an array to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeArrayContents(const CrasheeCrashReportWriter* const writer,
                               const char* const key,
                               const uintptr_t objectAddress,
                               int* limit)
{
    const void* object = (const void*)objectAddress;
    uintptr_t firstObject;
    if(crasheeobjc_arrayContents(object, &firstObject, 1) == 1)
    {
        writeMemoryContents(writer, key, firstObject, limit);
    }
}

/** Write out ivar information about an unknown object.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeUnknownObjectContents(const CrasheeCrashReportWriter* const writer,
                                       const char* const key,
                                       const uintptr_t objectAddress,
                                       int* limit)
{
    (*limit)--;
    const void* object = (const void*)objectAddress;
    CrasheeObjCIvar ivars[10];
    int8_t s8;
    int16_t s16;
    int sInt;
    int32_t s32;
    int64_t s64;
    uint8_t u8;
    uint16_t u16;
    unsigned int uInt;
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
    bool b;
    void* pointer;
    
    
    writer->beginObject(writer, key);
    {
        if(crasheeobjc_isTaggedPointer(object))
        {
            writer->addIntegerElement(writer, "tagged_payload", (int64_t)crasheeobjc_taggedPointerPayload(object));
        }
        else
        {
            const void* class = crasheeobjc_isaPointer(object);
            int ivarCount = crasheeobjc_ivarList(class, ivars, sizeof(ivars)/sizeof(*ivars));
            *limit -= ivarCount;
            for(int i = 0; i < ivarCount; i++)
            {
                CrasheeObjCIvar* ivar = &ivars[i];
                switch(ivar->type[0])
                {
                    case 'c':
                        crasheeobjc_ivarValue(object, ivar->index, &s8);
                        writer->addIntegerElement(writer, ivar->name, s8);
                        break;
                    case 'i':
                        crasheeobjc_ivarValue(object, ivar->index, &sInt);
                        writer->addIntegerElement(writer, ivar->name, sInt);
                        break;
                    case 's':
                        crasheeobjc_ivarValue(object, ivar->index, &s16);
                        writer->addIntegerElement(writer, ivar->name, s16);
                        break;
                    case 'l':
                        crasheeobjc_ivarValue(object, ivar->index, &s32);
                        writer->addIntegerElement(writer, ivar->name, s32);
                        break;
                    case 'q':
                        crasheeobjc_ivarValue(object, ivar->index, &s64);
                        writer->addIntegerElement(writer, ivar->name, s64);
                        break;
                    case 'C':
                        crasheeobjc_ivarValue(object, ivar->index, &u8);
                        writer->addUIntegerElement(writer, ivar->name, u8);
                        break;
                    case 'I':
                        crasheeobjc_ivarValue(object, ivar->index, &uInt);
                        writer->addUIntegerElement(writer, ivar->name, uInt);
                        break;
                    case 'S':
                        crasheeobjc_ivarValue(object, ivar->index, &u16);
                        writer->addUIntegerElement(writer, ivar->name, u16);
                        break;
                    case 'L':
                        crasheeobjc_ivarValue(object, ivar->index, &u32);
                        writer->addUIntegerElement(writer, ivar->name, u32);
                        break;
                    case 'Q':
                        crasheeobjc_ivarValue(object, ivar->index, &u64);
                        writer->addUIntegerElement(writer, ivar->name, u64);
                        break;
                    case 'f':
                        crasheeobjc_ivarValue(object, ivar->index, &f32);
                        writer->addFloatingPointElement(writer, ivar->name, f32);
                        break;
                    case 'd':
                        crasheeobjc_ivarValue(object, ivar->index, &f64);
                        writer->addFloatingPointElement(writer, ivar->name, f64);
                        break;
                    case 'B':
                        crasheeobjc_ivarValue(object, ivar->index, &b);
                        writer->addBooleanElement(writer, ivar->name, b);
                        break;
                    case '*':
                    case '@':
                    case '#':
                    case ':':
                        crasheeobjc_ivarValue(object, ivar->index, &pointer);
                        writeMemoryContents(writer, ivar->name, (uintptr_t)pointer, limit);
                        break;
                    default:
                        CrasheeLOG_DEBUG("%s: Unknown ivar type [%s]", ivar->name, ivar->type);
                }
            }
        }
    }
    writer->endContainer(writer);
}

static bool isRestrictedClass(const char* name)
{
    if(g_introspectionRules.restrictedClasses != NULL)
    {
        for(int i = 0; i < g_introspectionRules.restrictedClassesCount; i++)
        {
            if(strcmp(name, g_introspectionRules.restrictedClasses[i]) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static bool writeObjCObject(const CrasheeCrashReportWriter* const writer,
                            const uintptr_t address,
                            int* limit)
{
#if CrasheeCRASH_HAS_OBJC
    const void* object = (const void*)address;
    switch(crasheeobjc_objectType(object))
    {
        case CrasheeObjCTypeClass:
            writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_Class);
            writer->addStringElement(writer, CrasheeCrashField_Class, crasheeobjc_className(object));
            return true;
        case CrasheeObjCTypeObject:
        {
            writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_Object);
            const char* className = crasheeobjc_objectClassName(object);
            writer->addStringElement(writer, CrasheeCrashField_Class, className);
            if(!isRestrictedClass(className))
            {
                switch(crasheeobjc_objectClassType(object))
                {
                    case CrasheeObjCClassTypeString:
                        writeNSStringContents(writer, CrasheeCrashField_Value, address, limit);
                        return true;
                    case CrasheeObjCClassTypeURL:
                        writeURLContents(writer, CrasheeCrashField_Value, address, limit);
                        return true;
                    case CrasheeObjCClassTypeDate:
                        writeDateContents(writer, CrasheeCrashField_Value, address, limit);
                        return true;
                    case CrasheeObjCClassTypeArray:
                        if(*limit > 0)
                        {
                            writeArrayContents(writer, CrasheeCrashField_FirstObject, address, limit);
                        }
                        return true;
                    case CrasheeObjCClassTypeNumber:
                        writeNumberContents(writer, CrasheeCrashField_Value, address, limit);
                        return true;
                    case CrasheeObjCClassTypeDictionary:
                    case CrasheeObjCClassTypeException:
                        // TODO: Implement these.
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, CrasheeCrashField_Ivars, address, limit);
                        }
                        return true;
                    case CrasheeObjCClassTypeUnknown:
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, CrasheeCrashField_Ivars, address, limit);
                        }
                        return true;
                }
            }
            break;
        }
        case CrasheeObjCTypeBlock:
            writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_Block);
            const char* className = crasheeobjc_objectClassName(object);
            writer->addStringElement(writer, CrasheeCrashField_Class, className);
            return true;
        case CrasheeObjCTypeUnknown:
            break;
    }
#endif

    return false;
}

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const CrasheeCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit)
{
    (*limit)--;
    const void* object = (const void*)address;
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, CrasheeCrashField_Address, address);
        if(!writeObjCObject(writer, address, limit))
        {
            if(object == NULL)
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_NullPointer);
            }
            else if(isValidString(object))
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_String);
                writer->addStringElement(writer, CrasheeCrashField_Value, (const char*)object);
            }
            else
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashMemType_Unknown);
            }
        }
    }
    writer->endContainer(writer);
}

static bool isValidPointer(const uintptr_t address)
{
    if(address == (uintptr_t)NULL)
    {
        return false;
    }

#if CrasheeCRASH_HAS_OBJC
    if(crasheeobjc_isTaggedPointer((const void*)address))
    {
        if(!crasheeobjc_isValidTaggedPointer((const void*)address))
        {
            return false;
        }
    }
#endif

    return true;
}

static bool isNotableAddress(const uintptr_t address)
{
    if(!isValidPointer(address))
    {
        return false;
    }
    
    const void* object = (const void*)address;

#if CrasheeCRASH_HAS_OBJC

    if(crasheeobjc_objectType(object) != CrasheeObjCTypeUnknown)
    {
        return true;
    }
#endif

    if(isValidString(object))
    {
        return true;
    }

    return false;
}

/** Write the contents of a memory location only if it contains notable data.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 */
static void writeMemoryContentsIfNotable(const CrasheeCrashReportWriter* const writer,
                                         const char* const key,
                                         const uintptr_t address)
{
    if(isNotableAddress(address))
    {
        int limit = kDefaultMemorySearchDepth;
        writeMemoryContents(writer, key, address, &limit);
    }
}

/** Look for a hex value in a string and try to write whatever it references.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param string The string to search.
 */
static void writeAddressReferencedByString(const CrasheeCrashReportWriter* const writer,
                                           const char* const key,
                                           const char* string)
{
    uint64_t address = 0;
    if(string == NULL || !crasheestring_extractHexValue(string, (int)strlen(string), &address))
    {
        return;
    }
    
    int limit = kDefaultMemorySearchDepth;
    writeMemoryContents(writer, key, (uintptr_t)address, &limit);
}

#pragma mark Backtrace

/** Write a backtrace to the report.
 *
 * @param writer The writer to write the backtrace to.
 *
 * @param key The object key, if needed.
 *
 * @param stackCursor The stack cursor to read from.
 */
static void writeBacktrace(const CrasheeCrashReportWriter* const writer,
                           const char* const key,
                           CrasheeStackCursor* stackCursor)
{
    writer->beginObject(writer, key);
    {
        writer->beginArray(writer, CrasheeCrashField_Contents);
        {
            while(stackCursor->advanceCursor(stackCursor))
            {
                writer->beginObject(writer, NULL);
                {
                    if(stackCursor->symbolicate(stackCursor))
                    {
                        if(stackCursor->stackEntry.imageName != NULL)
                        {
                            writer->addStringElement(writer, CrasheeCrashField_ObjectName, crasheefu_lastPathEntry(stackCursor->stackEntry.imageName));
                        }
                        writer->addUIntegerElement(writer, CrasheeCrashField_ObjectAddr, stackCursor->stackEntry.imageAddress);
                        if(stackCursor->stackEntry.symbolName != NULL)
                        {
                            writer->addStringElement(writer, CrasheeCrashField_SymbolName, stackCursor->stackEntry.symbolName);
                        }
                        writer->addUIntegerElement(writer, CrasheeCrashField_SymbolAddr, stackCursor->stackEntry.symbolAddress);
                    }
                    writer->addUIntegerElement(writer, CrasheeCrashField_InstructionAddr, stackCursor->stackEntry.address);
                }
                writer->endContainer(writer);
            }
        }
        writer->endContainer(writer);
        writer->addIntegerElement(writer, CrasheeCrashField_Skipped, 0);
    }
    writer->endContainer(writer);
}
                              

#pragma mark Stack

/** Write a dump of the stack contents to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param isStackOverflow If true, the stack has overflowed.
 */
static void writeStackContents(const CrasheeCrashReportWriter* const writer,
                               const char* const key,
                               const struct CrasheeMachineContext* const machineContext,
                               const bool isStackOverflow)
{
    uintptr_t sp = crasheecpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(kStackContentsPushedDistance * (int)sizeof(sp) * crasheecpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(kStackContentsPoppedDistance * (int)sizeof(sp) * crasheecpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, CrasheeCrashField_GrowDirection, crasheecpu_stackGrowDirection() > 0 ? "+" : "-");
        writer->addUIntegerElement(writer, CrasheeCrashField_DumpStart, lowAddress);
        writer->addUIntegerElement(writer, CrasheeCrashField_DumpEnd, highAddress);
        writer->addUIntegerElement(writer, CrasheeCrashField_StackPtr, sp);
        writer->addBooleanElement(writer, CrasheeCrashField_Overflow, isStackOverflow);
        uint8_t stackBuffer[kStackContentsTotalDistance * sizeof(sp)];
        int copyLength = (int)(highAddress - lowAddress);
        if(crasheemem_copySafely((void*)lowAddress, stackBuffer, copyLength))
        {
            writer->addDataElement(writer, CrasheeCrashField_Contents, (void*)stackBuffer, copyLength);
        }
        else
        {
            writer->addStringElement(writer, CrasheeCrashField_Error, "Stack contents not accessible");
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses near the stack pointer (above and below).
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param backDistance The distance towards the beginning of the stack to check.
 *
 * @param forwardDistance The distance past the end of the stack to check.
 */
static void writeNotableStackContents(const CrasheeCrashReportWriter* const writer,
                                      const struct CrasheeMachineContext* const machineContext,
                                      const int backDistance,
                                      const int forwardDistance)
{
    uintptr_t sp = crasheecpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(backDistance * (int)sizeof(sp) * crasheecpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(forwardDistance * (int)sizeof(sp) * crasheecpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    uintptr_t contentsAsPointer;
    char nameBuffer[40];
    for(uintptr_t address = lowAddress; address < highAddress; address += sizeof(address))
    {
        if(crasheemem_copySafely((void*)address, &contentsAsPointer, sizeof(contentsAsPointer)))
        {
            sprintf(nameBuffer, "stack@%p", (void*)address);
            writeMemoryContentsIfNotable(writer, nameBuffer, contentsAsPointer);
        }
    }
}


#pragma mark Registers

/** Write the contents of all regular registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeBasicRegisters(const CrasheeCrashReportWriter* const writer,
                                const char* const key,
                                const struct CrasheeMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = crasheecpu_numRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = crasheecpu_registerName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer, registerName,
                                       crasheecpu_registerValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write the contents of all exception registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeExceptionRegisters(const CrasheeCrashReportWriter* const writer,
                                    const char* const key,
                                    const struct CrasheeMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = crasheecpu_numExceptionRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = crasheecpu_exceptionRegisterName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer,registerName,
                                       crasheecpu_exceptionRegisterValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write all applicable registers.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeRegisters(const CrasheeCrashReportWriter* const writer,
                           const char* const key,
                           const struct CrasheeMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeBasicRegisters(writer, CrasheeCrashField_Basic, machineContext);
        if(crasheemc_hasValidExceptionRegisters(machineContext))
        {
            writeExceptionRegisters(writer, CrasheeCrashField_Exception, machineContext);
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses contained in the CPU registers.
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableRegisters(const CrasheeCrashReportWriter* const writer,
                                  const struct CrasheeMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    const int numRegisters = crasheecpu_numRegisters();
    for(int reg = 0; reg < numRegisters; reg++)
    {
        registerName = crasheecpu_registerName(reg);
        if(registerName == NULL)
        {
            snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
            registerName = registerNameBuff;
        }
        writeMemoryContentsIfNotable(writer,
                                     registerName,
                                     (uintptr_t)crasheecpu_registerValue(machineContext, reg));
    }
}

#pragma mark Thread-specific

/** Write any notable addresses in the stack or registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableAddresses(const CrasheeCrashReportWriter* const writer,
                                  const char* const key,
                                  const struct CrasheeMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeNotableRegisters(writer, machineContext);
        writeNotableStackContents(writer,
                                  machineContext,
                                  kStackNotableSearchBackDistance,
                                  kStackNotableSearchForwardDistance);
    }
    writer->endContainer(writer);
}

/** Write information about a thread to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The context whose thread to write about.
 *
 * @param shouldWriteNotableAddresses If true, write any notable addresses found.
 */
static void writeThread(const CrasheeCrashReportWriter* const writer,
                        const char* const key,
                        const CrasheeCrash_MonitorContext* const crash,
                        const struct CrasheeMachineContext* const machineContext,
                        const int threadIndex,
                        const bool shouldWriteNotableAddresses)
{
    bool isCrashedThread = crasheemc_isCrashedContext(machineContext);
    CrasheeThread thread = crasheemc_getThreadFromContext(machineContext);
    CrasheeLOG_DEBUG("Writing thread %x (index %d). is crashed: %d", thread, threadIndex, isCrashedThread);

    CrasheeStackCursor stackCursor;
    bool hasBacktrace = getStackCursor(crash, machineContext, &stackCursor);

    writer->beginObject(writer, key);
    {
        if(hasBacktrace)
        {
            writeBacktrace(writer, CrasheeCrashField_Backtrace, &stackCursor);
        }
        if(crasheemc_canHaveCPUState(machineContext))
        {
            writeRegisters(writer, CrasheeCrashField_Registers, machineContext);
        }
        writer->addIntegerElement(writer, CrasheeCrashField_Index, threadIndex);
        const char* name = crasheeccd_getThreadName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, CrasheeCrashField_Name, name);
        }
        name = crasheeccd_getQueueName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, CrasheeCrashField_DispatchQueue, name);
        }
        writer->addBooleanElement(writer, CrasheeCrashField_Crashed, isCrashedThread);
        writer->addBooleanElement(writer, CrasheeCrashField_CurrentThread, thread == crasheethread_self());
        if(isCrashedThread)
        {
            writeStackContents(writer, CrasheeCrashField_Stack, machineContext, stackCursor.state.hasGivenUp);
            if(shouldWriteNotableAddresses)
            {
                writeNotableAddresses(writer, CrasheeCrashField_NotableAddresses, machineContext);
            }
        }
    }
    writer->endContainer(writer);
}

/** Write information about all threads to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeAllThreads(const CrasheeCrashReportWriter* const writer,
                            const char* const key,
                            const CrasheeCrash_MonitorContext* const crash,
                            bool writeNotableAddresses)
{
    const struct CrasheeMachineContext* const context = crash->offendingMachineContext;
    CrasheeThread offendingThread = crasheemc_getThreadFromContext(context);
    int threadCount = crasheemc_getThreadCount(context);
    CrasheeMC_NEW_CONTEXT(machineContext);

    // Fetch info for all threads.
    writer->beginArray(writer, key);
    {
        CrasheeLOG_DEBUG("Writing %d threads.", threadCount);
        for(int i = 0; i < threadCount; i++)
        {
            CrasheeThread thread = crasheemc_getThreadAtIndex(context, i);
            if(thread == offendingThread)
            {
                writeThread(writer, NULL, crash, context, i, writeNotableAddresses);
            }
            else
            {
                crasheemc_getContextForThread(thread, machineContext, false);
                writeThread(writer, NULL, crash, machineContext, i, writeNotableAddresses);
            }
        }
    }
    writer->endContainer(writer);
}

#pragma mark Global Report Data

/** Write information about a binary image to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param index Which image to write about.
 */
static void writeBinaryImage(const CrasheeCrashReportWriter* const writer,
                             const char* const key,
                             const int index)
{
    CrasheeBinaryImage image = {0};
    if(!crasheedl_getBinaryImage(index, &image))
    {
        return;
    }

    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageAddress, image.address);
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageVmAddress, image.vmAddress);
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageSize, image.size);
        writer->addStringElement(writer, CrasheeCrashField_Name, image.name);
        writer->addUUIDElement(writer, CrasheeCrashField_UUID, image.uuid);
        writer->addIntegerElement(writer, CrasheeCrashField_CPUType, image.cpuType);
        writer->addIntegerElement(writer, CrasheeCrashField_CPUSubType, image.cpuSubType);
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageMajorVersion, image.majorVersion);
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageMinorVersion, image.minorVersion);
        writer->addUIntegerElement(writer, CrasheeCrashField_ImageRevisionVersion, image.revisionVersion);
    }
    writer->endContainer(writer);
}

/** Write information about all images to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeBinaryImages(const CrasheeCrashReportWriter* const writer, const char* const key)
{
    const int imageCount = crasheedl_imageCount();

    writer->beginArray(writer, key);
    {
        for(int iImg = 0; iImg < imageCount; iImg++)
        {
            writeBinaryImage(writer, NULL, iImg);
        }
    }
    writer->endContainer(writer);
}

/** Write information about system memory to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeMemoryInfo(const CrasheeCrashReportWriter* const writer,
                            const char* const key,
                            const CrasheeCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, CrasheeCrashField_Size, monitorContext->System.memorySize);
        writer->addUIntegerElement(writer, CrasheeCrashField_Usable, monitorContext->System.usableMemory);
        writer->addUIntegerElement(writer, CrasheeCrashField_Free, monitorContext->System.freeMemory);
    }
    writer->endContainer(writer);
}

/** Write information about the error leading to the crash to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeError(const CrasheeCrashReportWriter* const writer,
                       const char* const key,
                       const CrasheeCrash_MonitorContext* const crash)
{
    writer->beginObject(writer, key);
    {
#if CrasheeCRASH_HOST_APPLE
        writer->beginObject(writer, CrasheeCrashField_Mach);
        {
            const char* machExceptionName = crasheemach_exceptionName(crash->mach.type);
            const char* machCodeName = crash->mach.code == 0 ? NULL : crasheemach_kernelReturnCodeName(crash->mach.code);
            writer->addUIntegerElement(writer, CrasheeCrashField_Exception, (unsigned)crash->mach.type);
            if(machExceptionName != NULL)
            {
                writer->addStringElement(writer, CrasheeCrashField_ExceptionName, machExceptionName);
            }
            writer->addUIntegerElement(writer, CrasheeCrashField_Code, (unsigned)crash->mach.code);
            if(machCodeName != NULL)
            {
                writer->addStringElement(writer, CrasheeCrashField_CodeName, machCodeName);
            }
            writer->addUIntegerElement(writer, CrasheeCrashField_Subcode, (size_t)crash->mach.subcode);
        }
        writer->endContainer(writer);
#endif
        writer->beginObject(writer, CrasheeCrashField_Signal);
        {
            const char* sigName = crasheesignal_signalName(crash->signal.signum);
            const char* sigCodeName = crasheesignal_signalCodeName(crash->signal.signum, crash->signal.sigcode);
            writer->addUIntegerElement(writer, CrasheeCrashField_Signal, (unsigned)crash->signal.signum);
            if(sigName != NULL)
            {
                writer->addStringElement(writer, CrasheeCrashField_Name, sigName);
            }
            writer->addUIntegerElement(writer, CrasheeCrashField_Code, (unsigned)crash->signal.sigcode);
            if(sigCodeName != NULL)
            {
                writer->addStringElement(writer, CrasheeCrashField_CodeName, sigCodeName);
            }
        }
        writer->endContainer(writer);

        writer->addUIntegerElement(writer, CrasheeCrashField_Address, crash->faultAddress);
        if(crash->crashReason != NULL)
        {
            writer->addStringElement(writer, CrasheeCrashField_Reason, crash->crashReason);
        }

        // Gather specific info.
        switch(crash->crashType)
        {
            case CrasheeCrashMonitorTypeMainThreadDeadlock:
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_Deadlock);
                break;
                
            case CrasheeCrashMonitorTypeMachException:
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_Mach);
                break;

            case CrasheeCrashMonitorTypeCPPException:
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_CPPException);
                writer->beginObject(writer, CrasheeCrashField_CPPException);
                {
                    writer->addStringElement(writer, CrasheeCrashField_Name, crash->CPPException.name);
                }
                writer->endContainer(writer);
                break;
            }
            case CrasheeCrashMonitorTypeNSException:
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_NSException);
                writer->beginObject(writer, CrasheeCrashField_NSException);
                {
                    writer->addStringElement(writer, CrasheeCrashField_Name, crash->NSException.name);
                    writer->addStringElement(writer, CrasheeCrashField_UserInfo, crash->NSException.userInfo);
                    writeAddressReferencedByString(writer, CrasheeCrashField_ReferencedObject, crash->crashReason);
                }
                writer->endContainer(writer);
                break;
            }
            case CrasheeCrashMonitorTypeSignal:
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_Signal);
                break;

            case CrasheeCrashMonitorTypeUserReported:
            {
                writer->addStringElement(writer, CrasheeCrashField_Type, CrasheeCrashExcType_User);
                writer->beginObject(writer, CrasheeCrashField_UserReported);
                {
                    writer->addStringElement(writer, CrasheeCrashField_Name, crash->userException.name);
                    if(crash->userException.language != NULL)
                    {
                        writer->addStringElement(writer, CrasheeCrashField_Language, crash->userException.language);
                    }
                    if(crash->userException.lineOfCode != NULL)
                    {
                        writer->addStringElement(writer, CrasheeCrashField_LineOfCode, crash->userException.lineOfCode);
                    }
                    if(crash->userException.customStackTrace != NULL)
                    {
                        writer->addJSONElement(writer, CrasheeCrashField_Backtrace, crash->userException.customStackTrace, true);
                    }
                }
                writer->endContainer(writer);
                break;
            }
            case CrasheeCrashMonitorTypeSystem:
            case CrasheeCrashMonitorTypeApplicationState:
            case CrasheeCrashMonitorTypeZombie:
                CrasheeLOG_ERROR("Crash monitor type 0x%x shouldn't be able to cause events!", crash->crashType);
                break;
        }
    }
    writer->endContainer(writer);
}

/** Write information about app runtime, etc to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param monitorContext The event monitor context.
 */
static void writeAppStats(const CrasheeCrashReportWriter* const writer,
                          const char* const key,
                          const CrasheeCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addBooleanElement(writer, CrasheeCrashField_AppActive, monitorContext->AppState.applicationIsActive);
        writer->addBooleanElement(writer, CrasheeCrashField_AppInFG, monitorContext->AppState.applicationIsInForeground);

        writer->addIntegerElement(writer, CrasheeCrashField_LaunchesSinceCrash, monitorContext->AppState.launchesSinceLastCrash);
        writer->addIntegerElement(writer, CrasheeCrashField_SessionsSinceCrash, monitorContext->AppState.sessionsSinceLastCrash);
        writer->addFloatingPointElement(writer, CrasheeCrashField_ActiveTimeSinceCrash, monitorContext->AppState.activeDurationSinceLastCrash);
        writer->addFloatingPointElement(writer, CrasheeCrashField_BGTimeSinceCrash, monitorContext->AppState.backgroundDurationSinceLastCrash);

        writer->addIntegerElement(writer, CrasheeCrashField_SessionsSinceLaunch, monitorContext->AppState.sessionsSinceLaunch);
        writer->addFloatingPointElement(writer, CrasheeCrashField_ActiveTimeSinceLaunch, monitorContext->AppState.activeDurationSinceLaunch);
        writer->addFloatingPointElement(writer, CrasheeCrashField_BGTimeSinceLaunch, monitorContext->AppState.backgroundDurationSinceLaunch);
    }
    writer->endContainer(writer);
}

/** Write information about this process.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeProcessState(const CrasheeCrashReportWriter* const writer,
                              const char* const key,
                              const CrasheeCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->ZombieException.address != 0)
        {
            writer->beginObject(writer, CrasheeCrashField_LastDeallocedNSException);
            {
                writer->addUIntegerElement(writer, CrasheeCrashField_Address, monitorContext->ZombieException.address);
                writer->addStringElement(writer, CrasheeCrashField_Name, monitorContext->ZombieException.name);
                writer->addStringElement(writer, CrasheeCrashField_Reason, monitorContext->ZombieException.reason);
                writeAddressReferencedByString(writer, CrasheeCrashField_ReferencedObject, monitorContext->ZombieException.reason);
            }
            writer->endContainer(writer);
        }
    }
    writer->endContainer(writer);
}

/** Write basic report information.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param type The report type.
 *
 * @param reportID The report ID.
 */
static void writeReportInfo(const CrasheeCrashReportWriter* const writer,
                            const char* const key,
                            const char* const type,
                            const char* const reportID,
                            const char* const processName)
{
    writer->beginObject(writer, key);
    {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        int64_t microseconds = ((int64_t)tp.tv_sec) * 1000000 + tp.tv_usec;
        
        writer->addStringElement(writer, CrasheeCrashField_Version, CrasheeCRASH_REPORT_VERSION);
        writer->addStringElement(writer, CrasheeCrashField_ID, reportID);
        writer->addStringElement(writer, CrasheeCrashField_ProcessName, processName);
        writer->addIntegerElement(writer, CrasheeCrashField_Timestamp, microseconds);
        writer->addStringElement(writer, CrasheeCrashField_Type, type);
    }
    writer->endContainer(writer);
}

static void writeRecrash(const CrasheeCrashReportWriter* const writer,
                         const char* const key,
                         const char* crashReportPath)
{
    writer->addJSONFileElement(writer, key, crashReportPath, true);
}


#pragma mark Setup

/** Prepare a report writer for use.
 *
 * @oaram writer The writer to prepare.
 *
 * @param context JSON writer contextual information.
 */
static void prepareReportWriter(CrasheeCrashReportWriter* const writer, CrasheeJSONEncodeContext* const context)
{
    writer->addBooleanElement = addBooleanElement;
    writer->addFloatingPointElement = addFloatingPointElement;
    writer->addIntegerElement = addIntegerElement;
    writer->addUIntegerElement = addUIntegerElement;
    writer->addStringElement = addStringElement;
    writer->addTextFileElement = addTextFileElement;
    writer->addTextFileLinesElement = addTextLinesFromFile;
    writer->addJSONFileElement = addJSONElementFromFile;
    writer->addDataElement = addDataElement;
    writer->beginDataElement = beginDataElement;
    writer->appendDataElement = appendDataElement;
    writer->endDataElement = endDataElement;
    writer->addUUIDElement = addUUIDElement;
    writer->addJSONElement = addJSONElement;
    writer->beginObject = beginObject;
    writer->beginArray = beginArray;
    writer->endContainer = endContainer;
    writer->context = context;
}


// ============================================================================
#pragma mark - Main API -
// ============================================================================

void crasheecrashreport_writeRecrashReport(const CrasheeCrash_MonitorContext* const monitorContext, const char* const path)
{
    char writeBuffer[1024];
    CrasheeBufferedWriter bufferedWriter;
    static char tempPath[CrasheeFU_MAX_PATH_LENGTH];
    strncpy(tempPath, path, sizeof(tempPath) - 10);
    strncpy(tempPath + strlen(tempPath) - 5, ".old", 5);
    CrasheeLOG_INFO("Writing recrash report to %s", path);

    if(rename(path, tempPath) < 0)
    {
        CrasheeLOG_ERROR("Could not rename %s to %s: %s", path, tempPath, strerror(errno));
    }
    if(!crasheefu_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    crasheeccd_freeze();

    CrasheeJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    CrasheeCrashReportWriter concreteWriter;
    CrasheeCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    crasheejson_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, CrasheeCrashField_Report);
    {
        writeRecrash(writer, CrasheeCrashField_RecrashReport, tempPath);
        crasheefu_flushBufferedWriter(&bufferedWriter);
        if(remove(tempPath) < 0)
        {
            CrasheeLOG_ERROR("Could not remove %s: %s", tempPath, strerror(errno));
        }
        writeReportInfo(writer,
                        CrasheeCrashField_Report,
                        CrasheeCrashReportType_Minimal,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, CrasheeCrashField_Crash);
        {
            writeError(writer, CrasheeCrashField_Error, monitorContext);
            crasheefu_flushBufferedWriter(&bufferedWriter);
            int threadIndex = crasheemc_indexOfThread(monitorContext->offendingMachineContext,
                                                 crasheemc_getThreadFromContext(monitorContext->offendingMachineContext));
            writeThread(writer,
                        CrasheeCrashField_CrashedThread,
                        monitorContext,
                        monitorContext->offendingMachineContext,
                        threadIndex,
                        false);
            crasheefu_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);
    }
    writer->endContainer(writer);

    crasheejson_endEncode(getJsonContext(writer));
    crasheefu_closeBufferedWriter(&bufferedWriter);
    crasheeccd_unfreeze();
}

static void writeSystemInfo(const CrasheeCrashReportWriter* const writer,
                            const char* const key,
                            const CrasheeCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, CrasheeCrashField_SystemName, monitorContext->System.systemName);
        writer->addStringElement(writer, CrasheeCrashField_SystemVersion, monitorContext->System.systemVersion);
        writer->addStringElement(writer, CrasheeCrashField_Machine, monitorContext->System.machine);
        writer->addStringElement(writer, CrasheeCrashField_Model, monitorContext->System.model);
        writer->addStringElement(writer, CrasheeCrashField_KernelVersion, monitorContext->System.kernelVersion);
        writer->addStringElement(writer, CrasheeCrashField_OSVersion, monitorContext->System.osVersion);
        writer->addBooleanElement(writer, CrasheeCrashField_Jailbroken, monitorContext->System.isJailbroken);
        writer->addStringElement(writer, CrasheeCrashField_BootTime, monitorContext->System.bootTime);
        writer->addStringElement(writer, CrasheeCrashField_AppStartTime, monitorContext->System.appStartTime);
        writer->addStringElement(writer, CrasheeCrashField_ExecutablePath, monitorContext->System.executablePath);
        writer->addStringElement(writer, CrasheeCrashField_Executable, monitorContext->System.executableName);
        writer->addStringElement(writer, CrasheeCrashField_BundleID, monitorContext->System.bundleID);
        writer->addStringElement(writer, CrasheeCrashField_BundleName, monitorContext->System.bundleName);
        writer->addStringElement(writer, CrasheeCrashField_BundleVersion, monitorContext->System.bundleVersion);
        writer->addStringElement(writer, CrasheeCrashField_BundleShortVersion, monitorContext->System.bundleShortVersion);
        writer->addStringElement(writer, CrasheeCrashField_AppUUID, monitorContext->System.appID);
        writer->addStringElement(writer, CrasheeCrashField_CPUArch, monitorContext->System.cpuArchitecture);
        writer->addIntegerElement(writer, CrasheeCrashField_CPUType, monitorContext->System.cpuType);
        writer->addIntegerElement(writer, CrasheeCrashField_CPUSubType, monitorContext->System.cpuSubType);
        writer->addIntegerElement(writer, CrasheeCrashField_BinaryCPUType, monitorContext->System.binaryCPUType);
        writer->addIntegerElement(writer, CrasheeCrashField_BinaryCPUSubType, monitorContext->System.binaryCPUSubType);
        writer->addStringElement(writer, CrasheeCrashField_TimeZone, monitorContext->System.timezone);
        writer->addStringElement(writer, CrasheeCrashField_ProcessName, monitorContext->System.processName);
        writer->addIntegerElement(writer, CrasheeCrashField_ProcessID, monitorContext->System.processID);
        writer->addIntegerElement(writer, CrasheeCrashField_ParentProcessID, monitorContext->System.parentProcessID);
        writer->addStringElement(writer, CrasheeCrashField_DeviceAppHash, monitorContext->System.deviceAppHash);
        writer->addStringElement(writer, CrasheeCrashField_BuildType, monitorContext->System.buildType);
        writer->addIntegerElement(writer, CrasheeCrashField_Storage, (int64_t)monitorContext->System.storageSize);

        writeMemoryInfo(writer, CrasheeCrashField_Memory, monitorContext);
        writeAppStats(writer, CrasheeCrashField_AppStats, monitorContext);
    }
    writer->endContainer(writer);

}

static void writeDebugInfo(const CrasheeCrashReportWriter* const writer,
                            const char* const key,
                            const CrasheeCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->consoleLogPath != NULL)
        {
            addTextLinesFromFile(writer, CrasheeCrashField_ConsoleLog, monitorContext->consoleLogPath);
        }
    }
    writer->endContainer(writer);
    
}

void crasheecrashreport_writeStandardReport(const CrasheeCrash_MonitorContext* const monitorContext, const char* const path)
{
    CrasheeLOG_INFO("Writing crash report to %s", path);
    char writeBuffer[1024];
    CrasheeBufferedWriter bufferedWriter;

    if(!crasheefu_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    crasheeccd_freeze();
    
    CrasheeJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    CrasheeCrashReportWriter concreteWriter;
    CrasheeCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    crasheejson_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, CrasheeCrashField_Report);
    {
        writeReportInfo(writer,
                        CrasheeCrashField_Report,
                        CrasheeCrashReportType_Standard,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writeBinaryImages(writer, CrasheeCrashField_BinaryImages);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writeProcessState(writer, CrasheeCrashField_ProcessState, monitorContext);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writeSystemInfo(writer, CrasheeCrashField_System, monitorContext);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, CrasheeCrashField_Crash);
        {
            writeError(writer, CrasheeCrashField_Error, monitorContext);
            crasheefu_flushBufferedWriter(&bufferedWriter);
            writeAllThreads(writer,
                            CrasheeCrashField_Threads,
                            monitorContext,
                            g_introspectionRules.enabled);
            crasheefu_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);

        if(g_userInfoJSON != NULL)
        {
            addJSONElement(writer, CrasheeCrashField_User, g_userInfoJSON, false);
            crasheefu_flushBufferedWriter(&bufferedWriter);
        }
        else
        {
            writer->beginObject(writer, CrasheeCrashField_User);
        }
        if(g_userSectionWriteCallback != NULL)
        {
            crasheefu_flushBufferedWriter(&bufferedWriter);
            if (monitorContext->currentSnapshotUserReported == false) {
                g_userSectionWriteCallback(writer);
            }
        }
        writer->endContainer(writer);
        crasheefu_flushBufferedWriter(&bufferedWriter);

        writeDebugInfo(writer, CrasheeCrashField_Debug, monitorContext);
    }
    writer->endContainer(writer);
    
    crasheejson_endEncode(getJsonContext(writer));
    crasheefu_closeBufferedWriter(&bufferedWriter);
    crasheeccd_unfreeze();
}



void crasheecrashreport_setUserInfoJSON(const char* const userInfoJSON)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CrasheeLOG_TRACE("set userInfoJSON to %p", userInfoJSON);

    pthread_mutex_lock(&mutex);
    if(g_userInfoJSON != NULL)
    {
        free((void*)g_userInfoJSON);
    }
    if(userInfoJSON == NULL)
    {
        g_userInfoJSON = NULL;
    }
    else
    {
        g_userInfoJSON = strdup(userInfoJSON);
    }
    pthread_mutex_unlock(&mutex);
}

void crasheecrashreport_setIntrospectMemory(bool shouldIntrospectMemory)
{
    g_introspectionRules.enabled = shouldIntrospectMemory;
}

void crasheecrashreport_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    const char** oldClasses = g_introspectionRules.restrictedClasses;
    int oldClassesLength = g_introspectionRules.restrictedClassesCount;
    const char** newClasses = NULL;
    int newClassesLength = 0;
    
    if(doNotIntrospectClasses != NULL && length > 0)
    {
        newClassesLength = length;
        newClasses = malloc(sizeof(*newClasses) * (unsigned)newClassesLength);
        if(newClasses == NULL)
        {
            CrasheeLOG_ERROR("Could not allocate memory");
            return;
        }
        
        for(int i = 0; i < newClassesLength; i++)
        {
            newClasses[i] = strdup(doNotIntrospectClasses[i]);
        }
    }
    
    g_introspectionRules.restrictedClasses = newClasses;
    g_introspectionRules.restrictedClassesCount = newClassesLength;
    
    if(oldClasses != NULL)
    {
        for(int i = 0; i < oldClassesLength; i++)
        {
            free((void*)oldClasses[i]);
        }
        free(oldClasses);
    }
}

void crasheecrashreport_setUserSectionWriteCallback(const CrasheeReportWriteCallback userSectionWriteCallback)
{
    CrasheeLOG_TRACE("Set userSectionWriteCallback to %p", userSectionWriteCallback);
    g_userSectionWriteCallback = userSectionWriteCallback;
}
