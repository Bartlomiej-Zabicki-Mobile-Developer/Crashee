//
//  CrasheeCrashC.c
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


#include "CrasheeCrashC.h"

#include "CrasheeCrashCachedData.h"
#include "CrasheeCrashReport.h"
#include "CrasheeCrashReportFixer.h"
#include "CrasheeCrashReportStore.h"
#include "Monitors/CrasheeCrashMonitor_User.h"
#include "Tools/CrasheeFileUtils.h"
#include "Tools/CrasheeObjC.h"
#include "Tools/CrasheeString.h"
#include "Monitors/CrasheeCrashMonitor_System.h"
#include "Monitors/CrasheeCrashMonitor_AppState.h"
#include "Monitors/CrasheeCrashMonitorContext.h"
#include "CrasheeSystemCapabilities.h"

//#define CrasheeLogger_LocalLevel TRACE
#include "Tools/CrasheeLogger.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    CrasheeApplicationStateNone,
    CrasheeApplicationStateDidBecomeActive,
    CrasheeApplicationStateWillResignActiveActive,
    CrasheeApplicationStateDidEnterBackground,
    CrasheeApplicationStateWillEnterForeground,
    CrasheeApplicationStateWillTerminate
} CrasheeApplicationState;

// ============================================================================
#pragma mark - Globals -
// ============================================================================

/** True if CrasheeCrash has been installed. */
static volatile bool g_installed = 0;

static bool g_shouldAddConsoleLogToReport = false;
static bool g_shouldPrintPreviousLog = false;
static char g_consoleLogPath[CrasheeFU_MAX_PATH_LENGTH];
static CrasheeCrashMonitorType g_monitoring = CrasheeCrashMonitorTypeProductionSafeMinimal;
static char g_lastCrashReportFilePath[CrasheeFU_MAX_PATH_LENGTH];
static CrasheeReportWrittenCallback g_reportWrittenCallback;
static CrasheeApplicationState g_lastApplicationState = CrasheeApplicationStateNone;

// ============================================================================
#pragma mark - Utility -
// ============================================================================

static void printPreviousLog(const char* filePath)
{
    char* data;
    int length;
    if(crasheefu_readEntireFile(filePath, &data, &length, 0))
    {
        printf("\nvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Previous Log vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n\n");
        printf("%s\n", data);
        free(data);
        printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");
        fflush(stdout);
    }
}

static void notifyOfBeforeInstallationState(void)
{
    CrasheeLOG_DEBUG("Notifying of pre-installation state");
    switch (g_lastApplicationState)
    {
        case CrasheeApplicationStateDidBecomeActive:
            return crasheecrash_notifyAppActive(true);
        case CrasheeApplicationStateWillResignActiveActive:
            return crasheecrash_notifyAppActive(false);
        case CrasheeApplicationStateDidEnterBackground:
            return crasheecrash_notifyAppInForeground(false);
        case CrasheeApplicationStateWillEnterForeground:
            return crasheecrash_notifyAppInForeground(true);
        case CrasheeApplicationStateWillTerminate:
            return crasheecrash_notifyAppTerminate();
        default:
            return;
    }
}

// ============================================================================
#pragma mark - Callbaccrashee -
// ============================================================================

/** Called when a crash occurs.
 *
 * This function gets passed as a callback to a crash handler.
 */
static void onCrash(struct CrasheeCrash_MonitorContext* monitorContext)
{
    if (monitorContext->currentSnapshotUserReported == false) {
        CrasheeLOG_DEBUG("Updating application state to note crash.");
        crasheecrashstate_notifyAppCrash();
    }
    monitorContext->consoleLogPath = g_shouldAddConsoleLogToReport ? g_consoleLogPath : NULL;

    if(monitorContext->crashedDuringCrashHandling)
    {
        crasheecrashreport_writeRecrashReport(monitorContext, g_lastCrashReportFilePath);
    }
    else
    {
        char crashReportFilePath[CrasheeFU_MAX_PATH_LENGTH];
        int64_t reportID = crasheecrs_getNextCrashReport(crashReportFilePath);
        strncpy(g_lastCrashReportFilePath, crashReportFilePath, sizeof(g_lastCrashReportFilePath));
        crasheecrashreport_writeStandardReport(monitorContext, crashReportFilePath);

        if(g_reportWrittenCallback)
        {
            g_reportWrittenCallback(reportID);
        }
    }
}


// ============================================================================
#pragma mark - API -
// ============================================================================

CrasheeCrashMonitorType crasheecrash_install(const char* appName, const char* const installPath)
{
    CrasheeLOG_DEBUG("Installing crash reporter.");

    if(g_installed)
    {
        CrasheeLOG_DEBUG("Crash reporter already installed.");
        return g_monitoring;
    }
    g_installed = 1;

    char path[CrasheeFU_MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/Reports", installPath);
    crasheefu_makePath(path);
    crasheecrs_initialize(appName, path);

    snprintf(path, sizeof(path), "%s/Data", installPath);
    crasheefu_makePath(path);
    snprintf(path, sizeof(path), "%s/Data/CrashState.json", installPath);
    crasheecrashstate_initialize(path);

    snprintf(g_consoleLogPath, sizeof(g_consoleLogPath), "%s/Data/ConsoleLog.txt", installPath);
    if(g_shouldPrintPreviousLog)
    {
        printPreviousLog(g_consoleLogPath);
    }
    crasheelog_setLogFilename(g_consoleLogPath, true);
    
    crasheeccd_init(60);

    crasheecm_setEventCallback(onCrash);
    CrasheeCrashMonitorType monitors = crasheecrash_setMonitoring(g_monitoring);

    CrasheeLOG_DEBUG("Installation complete.");

    notifyOfBeforeInstallationState();

    return monitors;
}

CrasheeCrashMonitorType crasheecrash_setMonitoring(CrasheeCrashMonitorType monitors)
{
    g_monitoring = monitors;
    
    if(g_installed)
    {
        crasheecm_setActiveMonitors(monitors);
        return crasheecm_getActiveMonitors();
    }
    // Return what we will be monitoring in future.
    return g_monitoring;
}

void crasheecrash_setUserInfoJSON(const char* const userInfoJSON)
{
    crasheecrashreport_setUserInfoJSON(userInfoJSON);
}

void crasheecrash_setSearchQueueNames(bool searchQueueNames)
{
    crasheeccd_setSearchQueueNames(searchQueueNames);
}

void crasheecrash_setIntrospectMemory(bool introspectMemory)
{
    crasheecrashreport_setIntrospectMemory(introspectMemory);
}

void crasheecrash_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    crasheecrashreport_setDoNotIntrospectClasses(doNotIntrospectClasses, length);
}

void crasheecrash_setCrashNotifyCallback(const CrasheeReportWriteCallback onCrashNotify)
{
    crasheecrashreport_setUserSectionWriteCallback(onCrashNotify);
}

void crasheecrash_setReportWrittenCallback(const CrasheeReportWrittenCallback onReportWrittenNotify)
{
    g_reportWrittenCallback = onReportWrittenNotify;
}

void crasheecrash_setAddConsoleLogToReport(bool shouldAddConsoleLogToReport)
{
    g_shouldAddConsoleLogToReport = shouldAddConsoleLogToReport;
}

void crasheecrash_setPrintPreviousLog(bool shouldPrintPreviousLog)
{
    g_shouldPrintPreviousLog = shouldPrintPreviousLog;
}

void crasheecrash_setMaxReportCount(int maxReportCount)
{
    crasheecrs_setMaxReportCount(maxReportCount);
}

void crasheecrash_reportUserException(const char* name,
                                 const char* reason,
                                 const char* language,
                                 const char* lineOfCode,
                                 const char* stackTrace,
                                 bool logAllThreads,
                                 bool terminateProgram)
{
    crasheecm_reportUserException(name,
                             reason,
                             language,
                             lineOfCode,
                             stackTrace,
                             logAllThreads,
                             terminateProgram);
    if(g_shouldAddConsoleLogToReport)
    {
        crasheelog_clearLogFile();
    }
}

void crasheecrash_notifyObjCLoad(void)
{
    crasheecrashstate_notifyObjCLoad();
}

void crasheecrash_notifyAppActive(bool isActive)
{
    if (g_installed)
    {
        crasheecrashstate_notifyAppActive(isActive);
    }
    g_lastApplicationState = isActive
        ? CrasheeApplicationStateDidBecomeActive
        : CrasheeApplicationStateWillResignActiveActive;
}

void crasheecrash_notifyAppInForeground(bool isInForeground)
{
    if (g_installed)
    {
        crasheecrashstate_notifyAppInForeground(isInForeground);
    }
    g_lastApplicationState = isInForeground
        ? CrasheeApplicationStateWillEnterForeground
        : CrasheeApplicationStateDidEnterBackground;
}

void crasheecrash_notifyAppTerminate(void)
{
    if (g_installed)
    {
        crasheecrashstate_notifyAppTerminate();
    }
    g_lastApplicationState = CrasheeApplicationStateWillTerminate;
}

void crasheecrash_notifyAppCrash(void)
{
    crasheecrashstate_notifyAppCrash();
}

int crasheecrash_getReportCount()
{
    return crasheecrs_getReportCount();
}

int crasheecrash_getReportIDs(int64_t* reportIDs, int count)
{
    return crasheecrs_getReportIDs(reportIDs, count);
}

char* crasheecrash_readReport(int64_t reportID)
{
    if(reportID <= 0)
    {
        CrasheeLOG_ERROR("Report ID was %" PRIx64, reportID);
        return NULL;
    }

    char* rawReport = crasheecrs_readReport(reportID);
    if(rawReport == NULL)
    {
        CrasheeLOG_ERROR("Failed to load report ID %" PRIx64, reportID);
        return NULL;
    }

    char* fixedReport = crasheecrf_fixupCrashReport(rawReport);
    if(fixedReport == NULL)
    {
        CrasheeLOG_ERROR("Failed to fixup report ID %" PRIx64, reportID);
    }

    free(rawReport);
    return fixedReport;
}

int64_t crasheecrash_addUserReport(const char* report, int reportLength)
{
    return crasheecrs_addUserReport(report, reportLength);
}

void crasheecrash_deleteAllReports()
{
    crasheecrs_deleteAllReports();
}

void crasheecrash_deleteReportWithID(int64_t reportID)
{
    crasheecrs_deleteReportWithID(reportID);
}
