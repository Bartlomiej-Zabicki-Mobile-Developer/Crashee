//
//  CrasheeCPU_x86_64.c
//
//  Created by Karl Stenerud on 2012-01-29.
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


#if defined (__x86_64__)


#include "CrasheeCPU.h"
#include "CrasheeCPU_Apple.h"
#include "CrasheeMachineContext.h"
#include "CrasheeMachineContext_Apple.h"

#include <stdlib.h>

//#define CrasheeLogger_LocalLevel TRACE
#include "CrasheeLogger.h"


static const char* g_registerNames[] =
{
    "rax", "rbx", "rcx", "rdx",
    "rdi", "rsi",
    "rbp", "rsp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "rip", "rflags",
    "cs", "fs", "gs"
};
static const int g_registerNamesCount =
sizeof(g_registerNames) / sizeof(*g_registerNames);


static const char* g_exceptionRegisterNames[] =
{
    "trapno", "err", "faultvaddr"
};
static const int g_exceptionRegisterNamesCount =
sizeof(g_exceptionRegisterNames) / sizeof(*g_exceptionRegisterNames);


uintptr_t crasheecpu_framePointer(const CrasheeMachineContext* const context)
{
    return context->machineContext.__ss.__rbp;
}

uintptr_t crasheecpu_stackPointer(const CrasheeMachineContext* const context)
{
    return context->machineContext.__ss.__rsp;
}

uintptr_t crasheecpu_instructionAddress(const CrasheeMachineContext* const context)
{
    return context->machineContext.__ss.__rip;
}

uintptr_t crasheecpu_linkRegister(__unused const CrasheeMachineContext* const context)
{
    return 0;
}

void crasheecpu_getState(CrasheeMachineContext* context)
{
    thread_t thread = context->thisThread;
    STRUCT_MCONTEXT_L* const machineContext = &context->machineContext;
    
    crasheecpu_i_fillState(thread, (thread_state_t)&machineContext->__ss, x86_THREAD_STATE64, x86_THREAD_STATE64_COUNT);
    crasheecpu_i_fillState(thread, (thread_state_t)&machineContext->__es, x86_EXCEPTION_STATE64, x86_EXCEPTION_STATE64_COUNT);
}

int crasheecpu_numRegisters(void)
{
    return g_registerNamesCount;
}

const char* crasheecpu_registerName(const int regNumber)
{
    if(regNumber < crasheecpu_numRegisters())
    {
        return g_registerNames[regNumber];
    }
    return NULL;
}

uint64_t crasheecpu_registerValue(const CrasheeMachineContext* const context, const int regNumber)
{
    switch(regNumber)
    {
        case 0:
            return context->machineContext.__ss.__rax;
        case 1:
            return context->machineContext.__ss.__rbx;
        case 2:
            return context->machineContext.__ss.__rcx;
        case 3:
            return context->machineContext.__ss.__rdx;
        case 4:
            return context->machineContext.__ss.__rdi;
        case 5:
            return context->machineContext.__ss.__rsi;
        case 6:
            return context->machineContext.__ss.__rbp;
        case 7:
            return context->machineContext.__ss.__rsp;
        case 8:
            return context->machineContext.__ss.__r8;
        case 9:
            return context->machineContext.__ss.__r9;
        case 10:
            return context->machineContext.__ss.__r10;
        case 11:
            return context->machineContext.__ss.__r11;
        case 12:
            return context->machineContext.__ss.__r12;
        case 13:
            return context->machineContext.__ss.__r13;
        case 14:
            return context->machineContext.__ss.__r14;
        case 15:
            return context->machineContext.__ss.__r15;
        case 16:
            return context->machineContext.__ss.__rip;
        case 17:
            return context->machineContext.__ss.__rflags;
        case 18:
            return context->machineContext.__ss.__cs;
        case 19:
            return context->machineContext.__ss.__fs;
        case 20:
            return context->machineContext.__ss.__gs;
    }

    CrasheeLOG_ERROR("Invalid register number: %d", regNumber);
    return 0;
}

int crasheecpu_numExceptionRegisters(void)
{
    return g_exceptionRegisterNamesCount;
}

const char* crasheecpu_exceptionRegisterName(const int regNumber)
{
    if(regNumber < crasheecpu_numExceptionRegisters())
    {
        return g_exceptionRegisterNames[regNumber];
    }
    CrasheeLOG_ERROR("Invalid register number: %d", regNumber);
    return NULL;
}

uint64_t crasheecpu_exceptionRegisterValue(const CrasheeMachineContext* const context, const int regNumber)
{
    switch(regNumber)
    {
        case 0:
            return context->machineContext.__es.__trapno;
        case 1:
            return context->machineContext.__es.__err;
        case 2:
            return context->machineContext.__es.__faultvaddr;
    }

    CrasheeLOG_ERROR("Invalid register number: %d", regNumber);
    return 0;
}

uintptr_t crasheecpu_faultAddress(const CrasheeMachineContext* const context)
{
    return context->machineContext.__es.__faultvaddr;
}

int crasheecpu_stackGrowDirection(void)
{
    return -1;
}

uintptr_t crasheecpu_normaliseInstructionPointer(uintptr_t ip)
{
    return ip;
}

#endif
