//
//  CrasheeCrashMonitor.c
//
//  Created by Karl Stenerud on 2012-02-12.
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


#include "CrasheeCrashMonitor.h"
#include "CrasheeCrashMonitorContext.h"
#include "CrasheeCrashMonitorType.h"

#include "CrasheeCrashMonitor_MachException.h"
#include "CrasheeCrashMonitor_CPPException.h"
#include "CrasheeCrashMonitor_NSException.h"
#include "CrasheeCrashMonitor_Signal.h"
#include "CrasheeCrashMonitor_System.h"
#include "CrasheeCrashMonitor_User.h"
#include "CrasheeCrashMonitor_AppState.h"
#include "../Tools/CrasheeDebug.h"
#include "../Tools/CrasheeThread.h"
#include "../CrasheeSystemCapabilities.h"

#include <memory.h>

//#define CrasheeLogger_LocalLevel TRACE
#include "../Tools/CrasheeLogger.h"


// ============================================================================
#pragma mark - Globals -
// ============================================================================

typedef struct
{
    CrasheeCrashMonitorType monitorType;
    CrasheeCrashMonitorAPI* (*getAPI)(void);
} Monitor;

static Monitor g_monitors[] =
{
#if CrasheeCRASH_HAS_MACH
    {
        .monitorType = CrasheeCrashMonitorTypeMachException,
        .getAPI = crasheecm_machexception_getAPI,
    },
#endif
#if CrasheeCRASH_HAS_SIGNAL
    {
        .monitorType = CrasheeCrashMonitorTypeSignal,
        .getAPI = crasheecm_signal_getAPI,
    },
#endif
#if CrasheeCRASH_HAS_OBJC
    {
        .monitorType = CrasheeCrashMonitorTypeNSException,
        .getAPI = crasheecm_nsexception_getAPI,
    },
#endif
    {
        .monitorType = CrasheeCrashMonitorTypeCPPException,
        .getAPI = crasheecm_cppexception_getAPI,
    },
    {
        .monitorType = CrasheeCrashMonitorTypeUserReported,
        .getAPI = crasheecm_user_getAPI,
    },
    {
        .monitorType = CrasheeCrashMonitorTypeSystem,
        .getAPI = crasheecm_system_getAPI,
    },
    {
        .monitorType = CrasheeCrashMonitorTypeApplicationState,
        .getAPI = crasheecm_appstate_getAPI,
    },
};
static int g_monitorsCount = sizeof(g_monitors) / sizeof(*g_monitors);

static CrasheeCrashMonitorType g_activeMonitors = CrasheeCrashMonitorTypeNone;

static bool g_handlingFatalException = false;
static bool g_crashedDuringExceptionHandling = false;
static bool g_requiresAsyncSafety = false;

static void (*g_onExceptionEvent)(struct CrasheeCrash_MonitorContext* monitorContext);

// ============================================================================
#pragma mark - API -
// ============================================================================

static inline CrasheeCrashMonitorAPI* getAPI(Monitor* monitor)
{
    if(monitor != NULL && monitor->getAPI != NULL)
    {
        return monitor->getAPI();
    }
    return NULL;
}

static inline void setMonitorEnabled(Monitor* monitor, bool isEnabled)
{
    CrasheeCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->setEnabled != NULL)
    {
        api->setEnabled(isEnabled);
    }
}

static inline bool isMonitorEnabled(Monitor* monitor)
{
    CrasheeCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->isEnabled != NULL)
    {
        return api->isEnabled();
    }
    return false;
}

static inline void addContextualInfoToEvent(Monitor* monitor, struct CrasheeCrash_MonitorContext* eventContext)
{
    CrasheeCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->addContextualInfoToEvent != NULL)
    {
        api->addContextualInfoToEvent(eventContext);
    }
}

void crasheecm_setEventCallback(void (*onEvent)(struct CrasheeCrash_MonitorContext* monitorContext))
{
    g_onExceptionEvent = onEvent;
}

void crasheecm_setActiveMonitors(CrasheeCrashMonitorType monitorTypes)
{
    if(crasheedebug_isBeingTraced() && (monitorTypes & CrasheeCrashMonitorTypeDebuggerUnsafe))
    {
        static bool hasWarned = false;
        if(!hasWarned)
        {
            hasWarned = true;
            CrasheeLOGBASIC_WARN("    ************************ Crash Handler Notice ************************");
            CrasheeLOGBASIC_WARN("    *     App is running in a debugger. Masking out unsafe monitors.     *");
            CrasheeLOGBASIC_WARN("    * This means that most crashes WILL NOT BE RECORDED while debugging! *");
            CrasheeLOGBASIC_WARN("    **********************************************************************");
        }
        monitorTypes &= CrasheeCrashMonitorTypeDebuggerSafe;
    }
    if(g_requiresAsyncSafety && (monitorTypes & CrasheeCrashMonitorTypeAsyncUnsafe))
    {
        CrasheeLOG_DEBUG("Async-safe environment detected. Masking out unsafe monitors.");
        monitorTypes &= CrasheeCrashMonitorTypeAsyncSafe;
    }

    CrasheeLOG_DEBUG("Changing active monitors from 0x%x tp 0x%x.", g_activeMonitors, monitorTypes);

    CrasheeCrashMonitorType activeMonitors = CrasheeCrashMonitorTypeNone;
    for(int i = 0; i < g_monitorsCount; i++)
    {
        Monitor* monitor = &g_monitors[i];
        bool isEnabled = monitor->monitorType & monitorTypes;
        setMonitorEnabled(monitor, isEnabled);
        if(isMonitorEnabled(monitor))
        {
            activeMonitors |= monitor->monitorType;
        }
        else
        {
            activeMonitors &= ~monitor->monitorType;
        }
    }

    CrasheeLOG_DEBUG("Active monitors are now 0x%x.", activeMonitors);
    g_activeMonitors = activeMonitors;
}

CrasheeCrashMonitorType crasheecm_getActiveMonitors()
{
    return g_activeMonitors;
}


// ============================================================================
#pragma mark - Private API -
// ============================================================================

bool crasheecm_notifyFatalExceptionCaptured(bool isAsyncSafeEnvironment)
{
    g_requiresAsyncSafety |= isAsyncSafeEnvironment; // Don't let it be unset.
    if(g_handlingFatalException)
    {
        g_crashedDuringExceptionHandling = true;
    }
    g_handlingFatalException = true;
    if(g_crashedDuringExceptionHandling)
    {
        CrasheeLOG_INFO("Detected crash in the crash reporter. Uninstalling CrasheeCrash.");
        crasheecm_setActiveMonitors(CrasheeCrashMonitorTypeNone);
    }
    return g_crashedDuringExceptionHandling;
}

void crasheecm_handleException(struct CrasheeCrash_MonitorContext* context)
{
    context->requiresAsyncSafety = g_requiresAsyncSafety;
    if(g_crashedDuringExceptionHandling)
    {
        context->crashedDuringCrashHandling = true;
    }
    for(int i = 0; i < g_monitorsCount; i++)
    {
        Monitor* monitor = &g_monitors[i];
        if(isMonitorEnabled(monitor))
        {
            addContextualInfoToEvent(monitor, context);
        }
    }

    g_onExceptionEvent(context);

    if (context->currentSnapshotUserReported) {
        g_handlingFatalException = false;
    } else {
        if(g_handlingFatalException && !g_crashedDuringExceptionHandling) {
            CrasheeLOG_DEBUG("Exception is fatal. Restoring original handlers.");
            crasheecm_setActiveMonitors(CrasheeCrashMonitorTypeNone);
        }
    }
}
