//
//  CrasheeJSONCodec.c
//
//  Created by Karl Stenerud on 2012-01-07.
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


#include "CrasheeJSONCodec.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


// ============================================================================
#pragma mark - Configuration -
// ============================================================================

/** Set to 1 if you're also compiling CrasheeLogger and want to use it here */
#ifndef CrasheeJSONCODEC_UseCrasheeLogger
    #define CrasheeJSONCODEC_UseCrasheeLogger 1
#endif

#if CrasheeJSONCODEC_UseCrasheeLogger
    #include "CrasheeLogger.h"
#else
    #define CrasheeLOG_DEBUG(FMT, ...)
#endif

/** The work buffer size to use when escaping string values.
 * There's little reason to change this since nothing ever gets truncated.
 */
#ifndef CrasheeJSONCODEC_WorkBufferSize
    #define CrasheeJSONCODEC_WorkBufferSize 512
#endif


// ============================================================================
#pragma mark - Helpers -
// ============================================================================

// Compiler hints for "if" statements
#define likely_if(x) if(__builtin_expect(x,1))
#define unlikely_if(x) if(__builtin_expect(x,0))

/** Used for writing hex string values. */
static char g_hexNybbles[] =
{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

const char* crasheejson_stringForError(const int error)
{
    switch (error)
    {
        case CrasheeJSON_ERROR_INVALID_CHARACTER:
            return "Invalid character";
        case CrasheeJSON_ERROR_DATA_TOO_LONG:
            return "Data too long";
        case CrasheeJSON_ERROR_CANNOT_ADD_DATA:
            return "Cannot add data";
        case CrasheeJSON_ERROR_INCOMPLETE:
            return "Incomplete data";
        case CrasheeJSON_ERROR_INVALID_DATA:
            return "Invalid data";
        default:
            return "(unknown error)";
    }
}


// ============================================================================
#pragma mark - Encode -
// ============================================================================

/** Add JSON encoded data to an external handler.
 * The external handler will decide how to handle the data (store/transmit/etc).
 *
 * @param context The encoding context.
 *
 * @param data The encoded data.
 *
 * @param length The length of the data.
 *
 * @return CrasheeJSON_OK if the data was handled successfully.
 */
#define addJSONData(CONTEXT,DATA,LENGTH) \
    (CONTEXT)->addJSONData(DATA, LENGTH, (CONTEXT)->userData)

/** Escape a string portion for use with JSON and send to data handler.
 *
 * @param context The JSON context.
 *
 * @param string The string to escape and write.
 *
 * @param length The length of the string.
 *
 * @return CrasheeJSON_OK if the data was handled successfully.
 */
static int appendEscapedString(CrasheeJSONEncodeContext* const context,
                               const char* restrict const string,
                               int length)
{
    char workBuffer[CrasheeJSONCODEC_WorkBufferSize];
    const char* const srcEnd = string + length;

    const char* restrict src = string;
    char* restrict dst = workBuffer;

    // Simple case (no escape or special characters)
    for(; src < srcEnd &&
        *src != '\\' &&
        *src != '\"' &&
        (unsigned char)*src >= ' '; src++)
    {
        *dst++ = *src;
    }

    // Deal with complicated case (if any)
    for(; src < srcEnd; src++)
    {
        switch(*src)
        {
            case '\\':
            case '\"':
                *dst++ = '\\';
                *dst++ = *src;
                break;
            case '\b':
                *dst++ = '\\';
                *dst++ = 'b';
                break;
            case '\f':
                *dst++ = '\\';
                *dst++ = 'f';
                break;
            case '\n':
                *dst++ = '\\';
                *dst++ = 'n';
                break;
            case '\r':
                *dst++ = '\\';
                *dst++ = 'r';
                break;
            case '\t':
                *dst++ = '\\';
                *dst++ = 't';
                break;
            default:
                unlikely_if((unsigned char)*src < ' ')
            {
                CrasheeLOG_DEBUG("Invalid character 0x%02x in string: %s",
                            *src, string);
                return CrasheeJSON_ERROR_INVALID_CHARACTER;
            }
                *dst++ = *src;
        }
    }
    int encLength = (int)(dst - workBuffer);
    dst -= encLength;
    return addJSONData(context, dst, encLength);
}

/** Escape a string for use with JSON and send to data handler.
 *
 * @param context The JSON context.
 *
 * @param string The string to escape and write.
 *
 * @param length The length of the string.
 *
 * @return CrasheeJSON_OK if the data was handled successfully.
 */
static int addEscapedString(CrasheeJSONEncodeContext* const context,
                            const char* restrict const string,
                            int length)
{
    int result = CrasheeJSON_OK;

    // Keep adding portions until the whole string has been processed.
    int offset = 0;
    while(offset < length)
    {
        int toAdd = length - offset;
        unlikely_if(toAdd > CrasheeJSONCODEC_WorkBufferSize / 2)
        {
            toAdd = CrasheeJSONCODEC_WorkBufferSize / 2;
        }
        result = appendEscapedString(context, string + offset, toAdd);
        unlikely_if(result != CrasheeJSON_OK)
        {
            break;
        }
        offset += toAdd;
    }
    return result;
}

/** Escape and quote a string for use with JSON and send to data handler.
 *
 * @param context The JSON context.
 *
 * @param string The string to escape and write.
 *
 * @param length The length of the string.
 *
 * @return CrasheeJSON_OK if the data was handled successfully.
 */
static int addQuotedEscapedString(CrasheeJSONEncodeContext* const context,
                                  const char* restrict const string,
                                  int length)
{
    int result;
    unlikely_if((result = addJSONData(context, "\"", 1)) != CrasheeJSON_OK)
    {
        return result;
    }
    result = addEscapedString(context, string, length);

    // Always close string, even if we failed to write its content
    int closeResult = addJSONData(context, "\"", 1);

    return result || closeResult;
}

int crasheejson_beginElement(CrasheeJSONEncodeContext* const context, const char* const name)
{
    int result = CrasheeJSON_OK;

    // Decide if a comma is warranted.
    unlikely_if(context->containerFirstEntry)
    {
        context->containerFirstEntry = false;
    }
    else
    {
        unlikely_if((result = addJSONData(context, ",", 1)) != CrasheeJSON_OK)
        {
            return result;
        }
    }

    // Pretty printing
    unlikely_if(context->prettyPrint && context->containerLevel > 0)
    {
        unlikely_if((result = addJSONData(context, "\n", 1)) != CrasheeJSON_OK)
        {
            return result;
        }
        for(int i = 0; i < context->containerLevel; i++)
        {
            unlikely_if((result = addJSONData(context, "    ", 4)) != CrasheeJSON_OK)
            {
                return result;
            }
        }
    }

    // Add a name field if we're in an object.
    if(context->isObject[context->containerLevel])
    {
        unlikely_if(name == NULL)
        {
            CrasheeLOG_DEBUG("Name was null inside an object");
            return CrasheeJSON_ERROR_INVALID_DATA;
        }
        unlikely_if((result = addQuotedEscapedString(context, name, (int)strlen(name))) != CrasheeJSON_OK)
        {
            return result;
        }
        unlikely_if(context->prettyPrint)
        {
            unlikely_if((result = addJSONData(context, ": ", 2)) != CrasheeJSON_OK)
            {
                return result;
            }
        }
        else
        {
            unlikely_if((result = addJSONData(context, ":", 1)) != CrasheeJSON_OK)
            {
                return result;
            }
        }
    }
    return result;
}

int crasheejson_addRawJSONData(CrasheeJSONEncodeContext* const context,
                          const char* const data,
                          const int length)
{
    return addJSONData(context, data, length);
}

int crasheejson_addBooleanElement(CrasheeJSONEncodeContext* const context,
                             const char* const name,
                             const bool value)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    if(value)
    {
        return addJSONData(context, "true", 4);
    }
    else
    {
        return addJSONData(context, "false", 5);
    }
}

int crasheejson_addFloatingPointElement(CrasheeJSONEncodeContext* const context,
                                   const char* const name,
                                   double value)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    char buff[30];
    sprintf(buff, "%lg", value);
    return addJSONData(context, buff, (int)strlen(buff));
}

int crasheejson_addIntegerElement(CrasheeJSONEncodeContext* const context,
                             const char* const name,
                             int64_t value)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    char buff[30];
    sprintf(buff, "%" PRId64, value);
    return addJSONData(context, buff, (int)strlen(buff));
}

int crasheejson_addUIntegerElement(CrasheeJSONEncodeContext* const context,
                             const char* const name,
                             uint64_t value)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    char buff[30];
    sprintf(buff, "%" PRIu64, value);
    return addJSONData(context, buff, (int)strlen(buff));
}

int crasheejson_addNullElement(CrasheeJSONEncodeContext* const context,
                          const char* const name)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    return addJSONData(context, "null", 4);
}

int crasheejson_addStringElement(CrasheeJSONEncodeContext* const context,
                            const char* const name,
                            const char* const value,
                            int length)
{
    unlikely_if(value == NULL)
    {
        return crasheejson_addNullElement(context, name);
    }
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    if(length == CrasheeJSON_SIZE_AUTOMATIC)
    {
        length = (int)strlen(value);
    }
    return addQuotedEscapedString(context, value, length);
}

int crasheejson_beginStringElement(CrasheeJSONEncodeContext* const context,
                              const char* const name)
{
    int result = crasheejson_beginElement(context, name);
    unlikely_if(result != CrasheeJSON_OK)
    {
        return result;
    }
    return addJSONData(context, "\"", 1);
}

int crasheejson_appendStringElement(CrasheeJSONEncodeContext* const context,
                               const char* const value,
                               int length)
{
    return addEscapedString(context, value, length);
}

int crasheejson_endStringElement(CrasheeJSONEncodeContext* const context)
{
    return addJSONData(context, "\"", 1);
}

int crasheejson_addDataElement(CrasheeJSONEncodeContext* const context,
                          const char* name,
                          const char* value,
                          int length)
{
    int result = CrasheeJSON_OK;
    result = crasheejson_beginDataElement(context, name);
    if(result == CrasheeJSON_OK)
    {
        result = crasheejson_appendDataElement(context, value, length);
    }
    if(result == CrasheeJSON_OK)
    {
        result = crasheejson_endDataElement(context);
    }
    return result;
}

int crasheejson_beginDataElement(CrasheeJSONEncodeContext* const context,
                            const char* const name)
{
    return crasheejson_beginStringElement(context, name);
}

int crasheejson_appendDataElement(CrasheeJSONEncodeContext* const context,
                             const char* const value,
                             int length)
{
    unsigned char* currentByte = (unsigned char*)value;
    unsigned char* end = currentByte + length;
    char chars[2];
    int result = CrasheeJSON_OK;
    while(currentByte < end)
    {
        chars[0] = g_hexNybbles[(*currentByte>>4)&15];
        chars[1] = g_hexNybbles[*currentByte&15];
        result = addJSONData(context, chars, sizeof(chars));
        if(result != CrasheeJSON_OK)
        {
            break;
        }
        currentByte++;
    }
    return result;
}

int crasheejson_endDataElement(CrasheeJSONEncodeContext* const context)
{
    return crasheejson_endStringElement(context);
}

int crasheejson_beginArray(CrasheeJSONEncodeContext* const context,
                      const char* const name)
{
    likely_if(context->containerLevel >= 0)
    {
        int result = crasheejson_beginElement(context, name);
        unlikely_if(result != CrasheeJSON_OK)
        {
            return result;
        }
    }

    context->containerLevel++;
    context->isObject[context->containerLevel] = false;
    context->containerFirstEntry = true;

    return addJSONData(context, "[", 1);
}

int crasheejson_beginObject(CrasheeJSONEncodeContext* const context,
                       const char* const name)
{
    likely_if(context->containerLevel >= 0)
    {
        int result = crasheejson_beginElement(context, name);
        unlikely_if(result != CrasheeJSON_OK)
        {
            return result;
        }
    }

    context->containerLevel++;
    context->isObject[context->containerLevel] = true;
    context->containerFirstEntry = true;

    return addJSONData(context, "{", 1);
}

int crasheejson_endContainer(CrasheeJSONEncodeContext* const context)
{
    unlikely_if(context->containerLevel <= 0)
    {
        return CrasheeJSON_OK;
    }

    bool isObject = context->isObject[context->containerLevel];
    context->containerLevel--;

    // Pretty printing
    unlikely_if(context->prettyPrint && !context->containerFirstEntry)
    {
        int result;
        unlikely_if((result = addJSONData(context, "\n", 1)) != CrasheeJSON_OK)
        {
            return result;
        }
        for(int i = 0; i < context->containerLevel; i++)
        {
            unlikely_if((result = addJSONData(context, "    ", 4)) != CrasheeJSON_OK)
            {
                return result;
            }
        }
    }
    context->containerFirstEntry = false;
    return addJSONData(context, isObject ? "}" : "]", 1);
}

void crasheejson_beginEncode(CrasheeJSONEncodeContext* const context,
                        bool prettyPrint,
                        CrasheeJSONAddDataFunc addJSONDataFunc,
                        void* const userData)
{
    memset(context, 0, sizeof(*context));
    context->addJSONData = addJSONDataFunc;
    context->userData = userData;
    context->prettyPrint = prettyPrint;
    context->containerFirstEntry = true;
}

int crasheejson_endEncode(CrasheeJSONEncodeContext* const context)
{
    int result = CrasheeJSON_OK;
    while(context->containerLevel > 0)
    {
        unlikely_if((result = crasheejson_endContainer(context)) != CrasheeJSON_OK)
        {
            return result;
        }
    }
    return result;
}


// ============================================================================
#pragma mark - Decode -
// ============================================================================

#define INV 0x11111

typedef struct
{
    /** Pointer to current work area in the buffer. */
    const char* bufferPtr;
    /** Pointer to the end of the buffer. */
    const char* bufferEnd;
    /** Pointer to a buffer for storing a decoded name. */
    char* nameBuffer;
    /** Length of the name buffer. */
    int nameBufferLength;
    /** Pointer to a buffer for storing a decoded string. */
    char* stringBuffer;
    /** Length of the string buffer. */
    int stringBufferLength;
    /** The callbaccrashee to call while decoding. */
    CrasheeJSONDecodeCallbaccrashee* const callbaccrashee;
    /** Data that was specified when calling crasheejson_decode(). */
    void* userData;
} CrasheeJSONDecodeContext;

/** Lookup table for converting hex values to integers.
 * INV (0x11111) is used to mark invalid characters so that any attempted
 * invalid nybble conversion is always > 0xffff.
 */
static const unsigned int g_hexConversion[] =
{
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, INV, INV, INV, INV, INV, INV,
    INV, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
    INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV, INV,
};


/** Encode a UTF-16 character to UTF-8. The dest pointer gets incremented
 * by however many bytes were needed for the conversion (1-4).
 *
 * @param character The UTF-16 character.
 *
 * @param dst Where to write the UTF-8 character.
 *
 * @return CrasheeJSON_OK if the encoding was successful.
 */
static int writeUTF8(unsigned int character, char** dst);

/** Decode a string value.
 *
 * @param context The decoding context.
 *
 * @param dstBuffer Buffer to hold the decoded string.
 *
 * @param dstBufferLength Length of the destination buffer.
 *
 * @return CrasheeJSON_OK if successful.
 */
static int decodeString(CrasheeJSONDecodeContext* context, char* dstBuffer, int dstBufferLength);

/** Decode a JSON element.
 *
 * @param name This element's name (or NULL if it has none).
 *
 * @param context The decoding context.
 *
 * @return CrasheeJSON_OK if successful.
 */
static int decodeElement(const char* const name,
                                CrasheeJSONDecodeContext* context);


/** Skip past any whitespace.
 *
 * @param CONTEXT The decoding context.
 */
#define SKIP_WHITESPACE(CONTEXT) \
while(CONTEXT->bufferPtr < CONTEXT->bufferEnd && isspace(*CONTEXT->bufferPtr)) \
{ \
    CONTEXT->bufferPtr++; \
}


/** Check if a character is valid for representing part of a floating point
 * number.
 *
 * @param ch The character to test.
 *
 * @return true if the character is valid for floating point.
 */
static inline bool isFPChar(char ch)
{
    switch(ch)
    {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '.': case 'e': case 'E': case '+': case '-':
            return true;
        default:
            return false;
    }
}

static int writeUTF8(unsigned int character, char** dst)
{
    likely_if(character <= 0x7f)
    {
        **dst = (char) character;
        (*dst)++;
        return CrasheeJSON_OK;
    }
    if(character <= 0x7ff)
    {
        (*dst)[0] = (char)(0xc0 | (character >> 6));
        (*dst)[1] = (char)(0x80 | (character & 0x3f));
        *dst += 2;
        return CrasheeJSON_OK;
    }
    if(character <= 0xffff)
    {
        (*dst)[0] = (char)(0xe0 | (character >> 12));
        (*dst)[1] = (char)(0x80 | ((character >> 6) & 0x3f));
        (*dst)[2] = (char)(0x80 | (character & 0x3f));
        *dst += 3;
        return CrasheeJSON_OK;
    }
    // RFC3629 restricts UTF-8 to end at 0x10ffff.
    if(character <= 0x10ffff)
    {
        (*dst)[0] = (char)(0xf0 | (character >> 18));
        (*dst)[1] = (char)(0x80 | ((character >> 12) & 0x3f));
        (*dst)[2] = (char)(0x80 | ((character >> 6) & 0x3f));
        (*dst)[3] = (char)(0x80 | (character & 0x3f));
        *dst += 4;
        return CrasheeJSON_OK;
    }

    // If we get here, the character cannot be converted to valid UTF-8.
    CrasheeLOG_DEBUG("Invalid unicode: 0x%04x", character);
    return CrasheeJSON_ERROR_INVALID_CHARACTER;
}

static int decodeString(CrasheeJSONDecodeContext* context, char* dstBuffer, int dstBufferLength)
{
    *dstBuffer = '\0';
    unlikely_if(*context->bufferPtr != '\"')
    {
        CrasheeLOG_DEBUG("Expected '\"' but got '%c'", *context->bufferPtr);
        return CrasheeJSON_ERROR_INVALID_CHARACTER;
    }

    const char* src = context->bufferPtr + 1;
    bool fastCopy = true;

    for(; src < context->bufferEnd && *src != '\"'; src++)
    {
        unlikely_if(*src == '\\')
        {
            fastCopy = false;
            src++;
        }
    }
    unlikely_if(src >= context->bufferEnd)
    {
        CrasheeLOG_DEBUG("Premature end of data");
        return CrasheeJSON_ERROR_INCOMPLETE;
    }
    const char* srcEnd = src;
    src = context->bufferPtr + 1;
    int length = (int)(srcEnd - src);
    if(length >= dstBufferLength)
    {
        CrasheeLOG_DEBUG("String is too long");
        return CrasheeJSON_ERROR_DATA_TOO_LONG;
    }

    context->bufferPtr = srcEnd + 1;

    // If no escape characters were encountered, we can fast copy.
    likely_if(fastCopy)
    {
        memcpy(dstBuffer, src, length);
        dstBuffer[length] = 0;
        return CrasheeJSON_OK;
    }

    char* dst = dstBuffer;

    for(; src < srcEnd; src++)
    {
        likely_if(*src != '\\')
        {
            *dst++ = *src;
        }
        else
        {
            src++;
            switch(*src)
            {
                case '"':
                    *dst++ = '\"';
                    continue;
                case '\\':
                    *dst++ = '\\';
                    continue;
                case 'n':
                    *dst++ = '\n';
                    continue;
                case 'r':
                    *dst++ = '\r';
                    continue;
                case '/':
                    *dst++ = '/';
                    continue;
                case 't':
                    *dst++ = '\t';
                    continue;
                case 'b':
                    *dst++ = '\b';
                    continue;
                case 'f':
                    *dst++ = '\f';
                    continue;
                case 'u':
                {
                    unlikely_if(src + 5 > srcEnd)
                    {
                        CrasheeLOG_DEBUG("Premature end of data");
                        return CrasheeJSON_ERROR_INCOMPLETE;
                    }
                    unsigned int accum =
                    g_hexConversion[src[1]] << 12 |
                    g_hexConversion[src[2]] << 8 |
                    g_hexConversion[src[3]] << 4 |
                    g_hexConversion[src[4]];
                    unlikely_if(accum > 0xffff)
                    {
                        CrasheeLOG_DEBUG("Invalid unicode sequence: %c%c%c%c",
                                    src[1], src[2], src[3], src[4]);
                        return CrasheeJSON_ERROR_INVALID_CHARACTER;
                    }

                    // UTF-16 Trail surrogate on its own.
                    unlikely_if(accum >= 0xdc00 && accum <= 0xdfff)
                    {
                        CrasheeLOG_DEBUG("Unexpected trail surrogate: 0x%04x",
                                    accum);
                        return CrasheeJSON_ERROR_INVALID_CHARACTER;
                    }

                    // UTF-16 Lead surrogate.
                    unlikely_if(accum >= 0xd800 && accum <= 0xdbff)
                    {
                        // Fetch trail surrogate.
                        unlikely_if(src + 11 > srcEnd)
                        {
                            CrasheeLOG_DEBUG("Premature end of data");
                            return CrasheeJSON_ERROR_INCOMPLETE;
                        }
                        unlikely_if(src[5] != '\\' ||
                                    src[6] != 'u')
                        {
                            CrasheeLOG_DEBUG("Expected \"\\u\" but got: \"%c%c\"",
                                        src[5], src[6]);
                            return CrasheeJSON_ERROR_INVALID_CHARACTER;
                        }
                        src += 6;
                        unsigned int accum2 =
                        g_hexConversion[src[1]] << 12 |
                        g_hexConversion[src[2]] << 8 |
                        g_hexConversion[src[3]] << 4 |
                        g_hexConversion[src[4]];
                        unlikely_if(accum2 < 0xdc00 || accum2 > 0xdfff)
                        {
                            CrasheeLOG_DEBUG("Invalid trail surrogate: 0x%04x",
                                        accum2);
                            return CrasheeJSON_ERROR_INVALID_CHARACTER;
                        }
                        // And combine 20 bit result.
                        accum = ((accum - 0xd800) << 10) | (accum2 - 0xdc00);
                    }

                    int result = writeUTF8(accum, &dst);
                    unlikely_if(result != CrasheeJSON_OK)
                    {
                        return result;
                    }
                    src += 4;
                    continue;
                }
                default:
                    CrasheeLOG_DEBUG("Invalid control character '%c'", *src);
                    return CrasheeJSON_ERROR_INVALID_CHARACTER;
            }
        }
    }

    *dst = 0;
    return CrasheeJSON_OK;
}

static int decodeElement(const char* const name, CrasheeJSONDecodeContext* context)
{
    SKIP_WHITESPACE(context);
    unlikely_if(context->bufferPtr >= context->bufferEnd)
    {
        CrasheeLOG_DEBUG("Premature end of data");
        return CrasheeJSON_ERROR_INCOMPLETE;
    }

    int sign = 1;
    int result;

    switch(*context->bufferPtr)
    {
        case '[':
        {
            context->bufferPtr++;
            result = context->callbaccrashee->onBeginArray(name, context->userData);
            unlikely_if(result != CrasheeJSON_OK) return result;
            while(context->bufferPtr < context->bufferEnd)
            {
                SKIP_WHITESPACE(context);
                unlikely_if(context->bufferPtr >= context->bufferEnd)
                {
                    break;
                }
                unlikely_if(*context->bufferPtr == ']')
                {
                    context->bufferPtr++;
                    return context->callbaccrashee->onEndContainer(context->userData);
                }
                result = decodeElement(NULL, context);
                unlikely_if(result != CrasheeJSON_OK) return result;
                SKIP_WHITESPACE(context);
                unlikely_if(context->bufferPtr >= context->bufferEnd)
                {
                    break;
                }
                likely_if(*context->bufferPtr == ',')
                {
                    context->bufferPtr++;
                }
            }
            CrasheeLOG_DEBUG("Premature end of data");
            return CrasheeJSON_ERROR_INCOMPLETE;
        }
        case '{':
        {
            context->bufferPtr++;
            result = context->callbaccrashee->onBeginObject(name, context->userData);
            unlikely_if(result != CrasheeJSON_OK) return result;
            while(context->bufferPtr < context->bufferEnd)
            {
                SKIP_WHITESPACE(context);
                unlikely_if(context->bufferPtr >= context->bufferEnd)
                {
                    break;
                }
                unlikely_if(*context->bufferPtr == '}')
                {
                    context->bufferPtr++;
                    return context->callbaccrashee->onEndContainer(context->userData);
                }
                result = decodeString(context, context->nameBuffer, context->nameBufferLength);
                unlikely_if(result != CrasheeJSON_OK) return result;
                SKIP_WHITESPACE(context);
                unlikely_if(context->bufferPtr >= context->bufferEnd)
                {
                    break;
                }
                unlikely_if(*context->bufferPtr != ':')
                {
                    CrasheeLOG_DEBUG("Expected ':' but got '%c'", *context->bufferPtr);
                    return CrasheeJSON_ERROR_INVALID_CHARACTER;
                }
                context->bufferPtr++;
                SKIP_WHITESPACE(context);
                result = decodeElement(context->nameBuffer, context);
                unlikely_if(result != CrasheeJSON_OK) return result;
                SKIP_WHITESPACE(context);
                unlikely_if(context->bufferPtr >= context->bufferEnd)
                {
                    break;
                }
                likely_if(*context->bufferPtr == ',')
                {
                    context->bufferPtr++;
                }
            }
            CrasheeLOG_DEBUG("Premature end of data");
            return CrasheeJSON_ERROR_INCOMPLETE;
        }
        case '\"':
        {
            result = decodeString(context, context->stringBuffer, context->stringBufferLength);
            unlikely_if(result != CrasheeJSON_OK) return result;
            result = context->callbaccrashee->onStringElement(name,
                                                context->stringBuffer,
                                                context->userData);
            return result;
        }
        case 'f':
        {
            unlikely_if(context->bufferEnd - context->bufferPtr < 5)
            {
                CrasheeLOG_DEBUG("Premature end of data");
                return CrasheeJSON_ERROR_INCOMPLETE;
            }
            unlikely_if(!(context->bufferPtr[1] == 'a' &&
                          context->bufferPtr[2] == 'l' &&
                          context->bufferPtr[3] == 's' &&
                          context->bufferPtr[4] == 'e'))
            {
                CrasheeLOG_DEBUG("Expected \"false\" but got \"f%c%c%c%c\"",
                            context->bufferPtr[1], context->bufferPtr[2], context->bufferPtr[3], context->bufferPtr[4]);
                return CrasheeJSON_ERROR_INVALID_CHARACTER;
            }
            context->bufferPtr += 5;
            return context->callbaccrashee->onBooleanElement(name, false, context->userData);
        }
        case 't':
        {
            unlikely_if(context->bufferEnd - context->bufferPtr < 4)
            {
                CrasheeLOG_DEBUG("Premature end of data");
                return CrasheeJSON_ERROR_INCOMPLETE;
            }
            unlikely_if(!(context->bufferPtr[1] == 'r' &&
                          context->bufferPtr[2] == 'u' &&
                          context->bufferPtr[3] == 'e'))
            {
                CrasheeLOG_DEBUG("Expected \"true\" but got \"t%c%c%c\"",
                            context->bufferPtr[1], context->bufferPtr[2], context->bufferPtr[3]);
                return CrasheeJSON_ERROR_INVALID_CHARACTER;
            }
            context->bufferPtr += 4;
            return context->callbaccrashee->onBooleanElement(name, true, context->userData);
        }
        case 'n':
        {
            unlikely_if(context->bufferEnd - context->bufferPtr < 4)
            {
                CrasheeLOG_DEBUG("Premature end of data");
                return CrasheeJSON_ERROR_INCOMPLETE;
            }
            unlikely_if(!(context->bufferPtr[1] == 'u' &&
                          context->bufferPtr[2] == 'l' &&
                          context->bufferPtr[3] == 'l'))
            {
                CrasheeLOG_DEBUG("Expected \"null\" but got \"n%c%c%c\"",
                            context->bufferPtr[1], context->bufferPtr[2], context->bufferPtr[3]);
                return CrasheeJSON_ERROR_INVALID_CHARACTER;
            }
            context->bufferPtr += 4;
            return context->callbaccrashee->onNullElement(name, context->userData);
        }
        case '-':
            sign = -1;
            context->bufferPtr++;
            unlikely_if(!isdigit(*context->bufferPtr))
        {
            CrasheeLOG_DEBUG("Not a digit: '%c'", *context->bufferPtr);
            return CrasheeJSON_ERROR_INVALID_CHARACTER;
        }
            // Fall through
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
            // Try integer conversion.
            int64_t accum = 0;
            const char* const start = context->bufferPtr;

            for(; context->bufferPtr < context->bufferEnd && isdigit(*context->bufferPtr); context->bufferPtr++)
            {
                accum = accum * 10 + (*context->bufferPtr - '0');
                unlikely_if(accum < 0)
                {
                    // Overflow
                    break;
                }
            }

            unlikely_if(context->bufferPtr >= context->bufferEnd)
            {
                CrasheeLOG_DEBUG("Premature end of data");
                return CrasheeJSON_ERROR_INCOMPLETE;
            }

            if(!isFPChar(*context->bufferPtr) && accum >= 0)
            {
                accum *= sign;
                return context->callbaccrashee->onIntegerElement(name, accum, context->userData);
            }

            while(context->bufferPtr < context->bufferEnd && isFPChar(*context->bufferPtr))
            {
                context->bufferPtr++;
            }

            unlikely_if(context->bufferPtr >= context->bufferEnd)
            {
                CrasheeLOG_DEBUG("Premature end of data");
                return CrasheeJSON_ERROR_INCOMPLETE;
            }

            // our buffer is not necessarily NULL-terminated, so
            // it would be undefined to call sscanf/sttod etc. directly.
            // instead we create a temporary string.
            double value;
            int len = (int)(context->bufferPtr - start);
            if(len >= context->stringBufferLength)
            {
                CrasheeLOG_DEBUG("Number is too long.");
                return CrasheeJSON_ERROR_DATA_TOO_LONG;
            }
            strncpy(context->stringBuffer, start, len);
            context->stringBuffer[len] = '\0';

            sscanf(context->stringBuffer, "%lg", &value);

            value *= sign;
            return context->callbaccrashee->onFloatingPointElement(name, value, context->userData);
        }
    }
    CrasheeLOG_DEBUG("Invalid character '%c'", *context->bufferPtr);
    return CrasheeJSON_ERROR_INVALID_CHARACTER;
}

int crasheejson_decode(const char* const data,
                  int length,
                  char* stringBuffer,
                  int stringBufferLength,
                  CrasheeJSONDecodeCallbaccrashee* const callbaccrashee,
                  void* const userData,
                  int* const errorOffset)
{
    char* nameBuffer = stringBuffer;
    int nameBufferLength = stringBufferLength / 4;
    stringBuffer = nameBuffer + nameBufferLength;
    stringBufferLength -= nameBufferLength;
    CrasheeJSONDecodeContext context =
    {
        .bufferPtr = (char*)data,
        .bufferEnd = (char*)data + length,
        .nameBuffer = nameBuffer,
        .nameBufferLength = nameBufferLength,
        .stringBuffer = stringBuffer,
        .stringBufferLength = (int)stringBufferLength,
        .callbaccrashee = callbaccrashee,
        .userData = userData
    };

    const char* ptr = data;

    int result = decodeElement(NULL, &context);
    likely_if(result == CrasheeJSON_OK)
    {
        result = callbaccrashee->onEndData(userData);
    }

    unlikely_if(result != CrasheeJSON_OK && errorOffset != NULL)
    {
        *errorOffset = (int)(ptr - data);
    }
    return result;
}

struct JSONFromFileContext;
typedef void (*UpdateDecoderCallback)(struct JSONFromFileContext* context);

typedef struct JSONFromFileContext
{
    CrasheeJSONEncodeContext* encodeContext;
    CrasheeJSONDecodeContext* decodeContext;
    char* bufferStart;
    const char* sourceFilename;
    int fd;
    bool isEOF;
    bool closeLastContainer;
    UpdateDecoderCallback updateDecoderCallback;
} JSONFromFileContext;

static void updateDecoder_doNothing(__unused struct JSONFromFileContext* context)
{
    
}

static void updateDecoder_readFile(struct JSONFromFileContext* context)
{
    likely_if(!context->isEOF)
    {
        const char* end = context->decodeContext->bufferEnd;
        char* start = context->bufferStart;
        const char* ptr = context->decodeContext->bufferPtr;
        int bufferLength = (int)(end - start);
        int remainingLength = (int)(end - ptr);
        unlikely_if(remainingLength < bufferLength / 2)
        {
            int fillLength = bufferLength - remainingLength;
            memcpy(start, ptr, remainingLength);
            context->decodeContext->bufferPtr = start;
            int bytesRead = (int)read(context->fd, start+remainingLength, (unsigned)fillLength);
            unlikely_if(bytesRead < fillLength)
            {
                if(bytesRead < 0)
                {
                    CrasheeLOG_ERROR("Error reading file %s: %s", context->sourceFilename, strerror(errno));
                }
                context->isEOF = true;
            }
        }
    }
}

static int addJSONFromFile_onBooleanElement(const char* const name,
                                            const bool value,
                                            void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_addBooleanElement(context->encodeContext, name, value);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onFloatingPointElement(const char* const name,
                                                  const double value,
                                                  void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_addFloatingPointElement(context->encodeContext, name, value);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onIntegerElement(const char* const name,
                                            const int64_t value,
                                            void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_addIntegerElement(context->encodeContext, name, value);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onNullElement(const char* const name,
                                         void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_addNullElement(context->encodeContext, name);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onStringElement(const char* const name,
                                           const char* const value,
                                           void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_addStringElement(context->encodeContext, name, value, (int)strlen(value));
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onBeginObject(const char* const name,
                                         void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_beginObject(context->encodeContext, name);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onBeginArray(const char* const name,
                                        void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = crasheejson_beginArray(context->encodeContext, name);
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onEndContainer(void* const userData)
{
    JSONFromFileContext* context = (JSONFromFileContext*)userData;
    int result = CrasheeJSON_OK;
    if(context->closeLastContainer || context->encodeContext->containerLevel > 2)
    {
        result = crasheejson_endContainer(context->encodeContext);
    }
    context->updateDecoderCallback(context);
    return result;
}

static int addJSONFromFile_onEndData(__unused void* const userData)
{
    return CrasheeJSON_OK;
}

int crasheejson_addJSONFromFile(CrasheeJSONEncodeContext* const encodeContext,
                           const char* restrict const name,
                           const char* restrict const filename,
                           const bool closeLastContainer)
{
    CrasheeJSONDecodeCallbaccrashee callbaccrashee =
    {
        .onBeginArray = addJSONFromFile_onBeginArray,
        .onBeginObject = addJSONFromFile_onBeginObject,
        .onBooleanElement = addJSONFromFile_onBooleanElement,
        .onEndContainer = addJSONFromFile_onEndContainer,
        .onEndData = addJSONFromFile_onEndData,
        .onFloatingPointElement = addJSONFromFile_onFloatingPointElement,
        .onIntegerElement = addJSONFromFile_onIntegerElement,
        .onNullElement = addJSONFromFile_onNullElement,
        .onStringElement = addJSONFromFile_onStringElement,
    };
    char nameBuffer[100] = {0};
    char stringBuffer[500] = {0};
    char fileBuffer[1000] = {0};
    CrasheeJSONDecodeContext decodeContext =
    {
        .bufferPtr = fileBuffer,
        .bufferEnd = fileBuffer + sizeof(fileBuffer),
        .nameBuffer = nameBuffer,
        .nameBufferLength = sizeof(nameBuffer),
        .stringBuffer = stringBuffer,
        .stringBufferLength = sizeof(stringBuffer),
        .callbaccrashee = &callbaccrashee,
        .userData = NULL,
    };

    int fd = open(filename, O_RDONLY);
    JSONFromFileContext jsonContext =
    {
        .encodeContext = encodeContext,
        .decodeContext = &decodeContext,
        .bufferStart = fileBuffer,
        .sourceFilename = filename,
        .fd = fd,
        .closeLastContainer = closeLastContainer,
        .isEOF = false,
        .updateDecoderCallback = updateDecoder_readFile,
    };
    decodeContext.userData = &jsonContext;
    int containerLevel = encodeContext->containerLevel;

    // Manually trigger a data load.
    decodeContext.bufferPtr = decodeContext.bufferEnd;
    jsonContext.updateDecoderCallback(&jsonContext);

    int result = decodeElement(name, &decodeContext);
    close(fd);
    while(closeLastContainer && encodeContext->containerLevel > containerLevel)
    {
        crasheejson_endContainer(encodeContext);
    }

    return result;
}

int crasheejson_addJSONElement(CrasheeJSONEncodeContext* const encodeContext,
                          const char* restrict const name,
                          const char* restrict const jsonData,
                          const int jsonDataLength,
                          const bool closeLastContainer)
{
    CrasheeJSONDecodeCallbaccrashee callbaccrashee =
    {
        .onBeginArray = addJSONFromFile_onBeginArray,
        .onBeginObject = addJSONFromFile_onBeginObject,
        .onBooleanElement = addJSONFromFile_onBooleanElement,
        .onEndContainer = addJSONFromFile_onEndContainer,
        .onEndData = addJSONFromFile_onEndData,
        .onFloatingPointElement = addJSONFromFile_onFloatingPointElement,
        .onIntegerElement = addJSONFromFile_onIntegerElement,
        .onNullElement = addJSONFromFile_onNullElement,
        .onStringElement = addJSONFromFile_onStringElement,
    };
    char nameBuffer[100] = {0};
    char stringBuffer[5000] = {0};
    CrasheeJSONDecodeContext decodeContext =
    {
        .bufferPtr = jsonData,
        .bufferEnd = jsonData + jsonDataLength,
        .nameBuffer = nameBuffer,
        .nameBufferLength = sizeof(nameBuffer),
        .stringBuffer = stringBuffer,
        .stringBufferLength = sizeof(stringBuffer),
        .callbaccrashee = &callbaccrashee,
        .userData = NULL,
    };
    
    JSONFromFileContext jsonContext =
    {
        .encodeContext = encodeContext,
        .decodeContext = &decodeContext,
        .bufferStart = (char*)jsonData,
        .sourceFilename = NULL,
        .fd = 0,
        .closeLastContainer = closeLastContainer,
        .isEOF = false,
        .updateDecoderCallback = updateDecoder_doNothing,
    };
    decodeContext.userData = &jsonContext;
    int containerLevel = encodeContext->containerLevel;
    
    int result = decodeElement(name, &decodeContext);
    while(closeLastContainer && encodeContext->containerLevel > containerLevel)
    {
        crasheejson_endContainer(encodeContext);
    }
    
    return result;
}
