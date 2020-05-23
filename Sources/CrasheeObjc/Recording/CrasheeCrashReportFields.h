//
//  CrasheeCrashReportFields.h
//
//  Created by Karl Stenerud on 2012-10-07.
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


#ifndef HDR_CrasheeCrashReportFields_h
#define HDR_CrasheeCrashReportFields_h


#pragma mark - Report Types -

#define CrasheeCrashReportType_Minimal          "minimal"
#define CrasheeCrashReportType_Standard         "standard"
#define CrasheeCrashReportType_Custom           "custom"


#pragma mark - Memory Types -

#define CrasheeCrashMemType_Block               "objc_block"
#define CrasheeCrashMemType_Class               "objc_class"
#define CrasheeCrashMemType_NullPointer         "null_pointer"
#define CrasheeCrashMemType_Object              "objc_object"
#define CrasheeCrashMemType_String              "string"
#define CrasheeCrashMemType_Unknown             "unknown"


#pragma mark - Exception Types -

#define CrasheeCrashExcType_CPPException        "cpp_exception"
#define CrasheeCrashExcType_Deadlock            "deadlock"
#define CrasheeCrashExcType_Mach                "mach"
#define CrasheeCrashExcType_NSException         "nsexception"
#define CrasheeCrashExcType_Signal              "signal"
#define CrasheeCrashExcType_User                "user"


#pragma mark - Common -

#define CrasheeCrashField_Address               "address"
#define CrasheeCrashField_Contents              "contents"
#define CrasheeCrashField_Exception             "exception"
#define CrasheeCrashField_FirstObject           "first_object"
#define CrasheeCrashField_Index                 "index"
#define CrasheeCrashField_Ivars                 "ivars"
#define CrasheeCrashField_Language              "language"
#define CrasheeCrashField_Name                  "name"
#define CrasheeCrashField_UserInfo              "userInfo"
#define CrasheeCrashField_ReferencedObject      "referenced_object"
#define CrasheeCrashField_Type                  "type"
#define CrasheeCrashField_UUID                  "uuid"
#define CrasheeCrashField_Value                 "value"

#define CrasheeCrashField_Error                 "error"
#define CrasheeCrashField_JSONData              "json_data"


#pragma mark - Notable Address -

#define CrasheeCrashField_Class                 "class"
#define CrasheeCrashField_LastDeallocObject     "last_deallocated_obj"


#pragma mark - Backtrace -

#define CrasheeCrashField_InstructionAddr       "instruction_addr"
#define CrasheeCrashField_LineOfCode            "line_of_code"
#define CrasheeCrashField_ObjectAddr            "object_addr"
#define CrasheeCrashField_ObjectName            "object_name"
#define CrasheeCrashField_SymbolAddr            "symbol_addr"
#define CrasheeCrashField_SymbolName            "symbol_name"


#pragma mark - Stack Dump -

#define CrasheeCrashField_DumpEnd               "dump_end"
#define CrasheeCrashField_DumpStart             "dump_start"
#define CrasheeCrashField_GrowDirection         "grow_direction"
#define CrasheeCrashField_Overflow              "overflow"
#define CrasheeCrashField_StackPtr              "stack_pointer"


#pragma mark - Thread Dump -

#define CrasheeCrashField_Backtrace             "backtrace"
#define CrasheeCrashField_Basic                 "basic"
#define CrasheeCrashField_Crashed               "crashed"
#define CrasheeCrashField_CurrentThread         "current_thread"
#define CrasheeCrashField_DispatchQueue         "dispatch_queue"
#define CrasheeCrashField_NotableAddresses      "notable_addresses"
#define CrasheeCrashField_Registers             "registers"
#define CrasheeCrashField_Skipped               "skipped"
#define CrasheeCrashField_Stack                 "stack"


#pragma mark - Binary Image -

#define CrasheeCrashField_CPUSubType            "cpu_subtype"
#define CrasheeCrashField_CPUType               "cpu_type"
#define CrasheeCrashField_ImageAddress          "image_addr"
#define CrasheeCrashField_ImageVmAddress        "image_vmaddr"
#define CrasheeCrashField_ImageSize             "image_size"
#define CrasheeCrashField_ImageMajorVersion     "major_version"
#define CrasheeCrashField_ImageMinorVersion     "minor_version"
#define CrasheeCrashField_ImageRevisionVersion  "revision_version"


#pragma mark - Memory -

#define CrasheeCrashField_Free                  "free"
#define CrasheeCrashField_Usable                "usable"


#pragma mark - Error -

#define CrasheeCrashField_Backtrace             "backtrace"
#define CrasheeCrashField_Code                  "code"
#define CrasheeCrashField_CodeName              "code_name"
#define CrasheeCrashField_CPPException          "cpp_exception"
#define CrasheeCrashField_ExceptionName         "exception_name"
#define CrasheeCrashField_Mach                  "mach"
#define CrasheeCrashField_NSException           "nsexception"
#define CrasheeCrashField_Reason                "reason"
#define CrasheeCrashField_Signal                "signal"
#define CrasheeCrashField_Subcode               "subcode"
#define CrasheeCrashField_UserReported          "user_reported"


#pragma mark - Process State -

#define CrasheeCrashField_LastDeallocedNSException "last_dealloced_nsexception"
#define CrasheeCrashField_ProcessState             "process"


#pragma mark - App Stats -

#define CrasheeCrashField_ActiveTimeSinceCrash  "active_time_since_last_crash"
#define CrasheeCrashField_ActiveTimeSinceLaunch "active_time_since_launch"
#define CrasheeCrashField_AppActive             "application_active"
#define CrasheeCrashField_AppInFG               "application_in_foreground"
#define CrasheeCrashField_BGTimeSinceCrash      "background_time_since_last_crash"
#define CrasheeCrashField_BGTimeSinceLaunch     "background_time_since_launch"
#define CrasheeCrashField_LaunchesSinceCrash    "launches_since_last_crash"
#define CrasheeCrashField_SessionsSinceCrash    "sessions_since_last_crash"
#define CrasheeCrashField_SessionsSinceLaunch   "sessions_since_launch"


#pragma mark - Report -

#define CrasheeCrashField_Crash                 "crash"
#define CrasheeCrashField_Debug                 "debug"
#define CrasheeCrashField_Diagnosis             "diagnosis"
#define CrasheeCrashField_ID                    "id"
#define CrasheeCrashField_ProcessName           "process_name"
#define CrasheeCrashField_Report                "report"
#define CrasheeCrashField_Timestamp             "timestamp"
#define CrasheeCrashField_Version               "version"

#pragma mark Minimal
#define CrasheeCrashField_CrashedThread         "crashed_thread"

#pragma mark Standard
#define CrasheeCrashField_AppStats              "application_stats"
#define CrasheeCrashField_BinaryImages          "binary_images"
#define CrasheeCrashField_System                "system"
#define CrasheeCrashField_Memory                "memory"
#define CrasheeCrashField_Threads               "threads"
#define CrasheeCrashField_User                  "user"
#define CrasheeCrashField_ConsoleLog            "console_log"

#pragma mark Incomplete
#define CrasheeCrashField_Incomplete            "incomplete"
#define CrasheeCrashField_RecrashReport         "recrash_report"

#pragma mark System
#define CrasheeCrashField_AppStartTime          "app_start_time"
#define CrasheeCrashField_AppUUID               "app_uuid"
#define CrasheeCrashField_BootTime              "boot_time"
#define CrasheeCrashField_BundleID              "CFBundleIdentifier"
#define CrasheeCrashField_BundleName            "CFBundleName"
#define CrasheeCrashField_BundleShortVersion    "CFBundleShortVersionString"
#define CrasheeCrashField_BundleVersion         "CFBundleVersion"
#define CrasheeCrashField_CPUArch               "cpu_arch"
#define CrasheeCrashField_CPUType               "cpu_type"
#define CrasheeCrashField_CPUSubType            "cpu_subtype"
#define CrasheeCrashField_BinaryCPUType         "binary_cpu_type"
#define CrasheeCrashField_BinaryCPUSubType      "binary_cpu_subtype"
#define CrasheeCrashField_DeviceAppHash         "device_app_hash"
#define CrasheeCrashField_Executable            "CFBundleExecutable"
#define CrasheeCrashField_ExecutablePath        "CFBundleExecutablePath"
#define CrasheeCrashField_Jailbroken            "jailbroken"
#define CrasheeCrashField_KernelVersion         "kernel_version"
#define CrasheeCrashField_Machine               "machine"
#define CrasheeCrashField_Model                 "model"
#define CrasheeCrashField_OSVersion             "os_version"
#define CrasheeCrashField_ParentProcessID       "parent_process_id"
#define CrasheeCrashField_ProcessID             "process_id"
#define CrasheeCrashField_ProcessName           "process_name"
#define CrasheeCrashField_Size                  "size"
#define CrasheeCrashField_Storage               "storage"
#define CrasheeCrashField_SystemName            "system_name"
#define CrasheeCrashField_SystemVersion         "system_version"
#define CrasheeCrashField_TimeZone              "time_zone"
#define CrasheeCrashField_BuildType             "build_type"

#endif
