//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation

public struct CrashReport: Codable {
    let binaryImages: [BinaryImage]
    var crash: Crash
    let debug, process: Debug
    let report: Report
    let system: System
    let user: Debug

    enum CodingKeys: String, CodingKey {
        case binaryImages = "binary_images"
        case crash, debug, process, report, system, user
    }
}

// MARK: - BinaryImage
public struct BinaryImage: Codable {
    let cpuSubtype, cpuType, imageAddr, imageSize: Int
    let imageVmaddr, majorVersion, minorVersion: Int
    let name: String
    let revisionVersion: Int
    let uuid: String

    enum CodingKeys: String, CodingKey {
        case cpuSubtype = "cpu_subtype"
        case cpuType = "cpu_type"
        case imageAddr = "image_addr"
        case imageSize = "image_size"
        case imageVmaddr = "image_vmaddr"
        case majorVersion = "major_version"
        case minorVersion = "minor_version"
        case name
        case revisionVersion = "revision_version"
        case uuid
    }
}

// MARK: - Crash
public struct Crash: Codable {
    let error: ErrorCrash
    var threads: [CrashThread]
    var diagnosis: String?
}

// MARK: - Error
public struct ErrorCrash: Codable {
    let address: Int
    let mach: Mach?
    let signal: Signal
    let type: String
    let exception: Exception?
}

// MARK: - Mach
public struct Mach: Codable {
    let code: Int
    let codeName: String
    let exception: Int
    let exceptionName: String
    let subcode: Int

    enum CodingKeys: String, CodingKey {
        case code
        case codeName = "code_name"
        case exception
        case exceptionName = "exception_name"
        case subcode
    }
}

// MARK: - Signal
public struct Signal: Codable {
    let code: Int
    let codeName, name: String
    let signal: Int

    enum CodingKeys: String, CodingKey {
        case code
        case codeName = "code_name"
        case name, signal
    }
}

// MARK: - Thread
public struct CrashThread: Codable {
    var backtrace: Backtrace
    let crashed, currentThread: Bool
    let index: Int
    let registers: Registers?
    let stack: Stack?
    let name: String?

    enum CodingKeys: String, CodingKey {
        case backtrace, crashed
        case currentThread = "current_thread"
        case index
        case registers, stack, name
    }
}

// MARK: - Backtrace
public struct Backtrace: Codable {
    var contents: [BacktraceContent]
    let skipped: Int
    
    mutating func replace(contents: [BacktraceContent]) {
        self.contents = contents
    }
}

// MARK: - Content
public struct BacktraceContent: Codable {
    let instructionAddr: Int
    let objectAddr: Int?
    let objectName: String?
    let symbolAddr: Int?
    let symbolName: String?

    enum CodingKeys: String, CodingKey {
        case instructionAddr = "instruction_addr"
        case objectAddr = "object_addr"
        case objectName = "object_name"
        case symbolAddr = "symbol_addr"
        case symbolName = "symbol_name"
    }
    
    var isEmpty: Bool { objectAddr == nil || objectName == nil }
}

// MARK: - Registers
public struct Registers: Codable {
    let basic: [String: Double]
    let exception: Exception?
}

// MARK: - Exception
public struct Exception: Codable {
    let esr, exception, far: Int
}

// MARK: - Stack
public struct Stack: Codable {
    let contents: String
    let dumpEnd, dumpStart: Int
    let growDirection: String
    let overflow: Bool
    let stackPointer: Int

    enum CodingKeys: String, CodingKey {
        case contents
        case dumpEnd = "dump_end"
        case dumpStart = "dump_start"
        case growDirection = "grow_direction"
        case overflow
        case stackPointer = "stack_pointer"
    }
}

// MARK: - Debug
public struct Debug: Codable {
}

// MARK: - Report
public struct Report: Codable {
    let id, processName: String
    let type, version: String

    enum CodingKeys: String, CodingKey {
        case id
        case processName = "process_name"
        case type, version
    }
}

// MARK: - System
public struct System: Codable {
    let cfBundleExecutable, cfBundleExecutablePath, cfBundleIdentifier, cfBundleName: String
    let cfBundleShortVersionString, cfBundleVersion: String
    let appUUID: String
    let applicationStats: ApplicationStats
    let binaryCPUSubtype, binaryCPUType: Int
    let buildType, cpuArch: String
    let cpuSubtype, cpuType: Int
    let deviceAppHash: String
    let jailbroken: Bool
    let kernelVersion, machine: String
    let memory: Memory
    let model, osVersion: String
    let parentProcessID, processID: Int
    let processName: String
    let storage: Int
    let systemName, systemVersion, timeZone: String

    enum CodingKeys: String, CodingKey {
        case cfBundleExecutable = "CFBundleExecutable"
        case cfBundleExecutablePath = "CFBundleExecutablePath"
        case cfBundleIdentifier = "CFBundleIdentifier"
        case cfBundleName = "CFBundleName"
        case cfBundleShortVersionString = "CFBundleShortVersionString"
        case cfBundleVersion = "CFBundleVersion"
        case appUUID = "app_uuid"
        case applicationStats = "application_stats"
        case binaryCPUSubtype = "binary_cpu_subtype"
        case binaryCPUType = "binary_cpu_type"
        case buildType = "build_type"
        case cpuArch = "cpu_arch"
        case cpuSubtype = "cpu_subtype"
        case cpuType = "cpu_type"
        case deviceAppHash = "device_app_hash"
        case jailbroken
        case kernelVersion = "kernel_version"
        case machine, memory, model
        case osVersion = "os_version"
        case parentProcessID = "parent_process_id"
        case processID = "process_id"
        case processName = "process_name"
        case storage
        case systemName = "system_name"
        case systemVersion = "system_version"
        case timeZone = "time_zone"
    }
}

// MARK: - ApplicationStats
public struct ApplicationStats: Codable {
    let activeTimeSinceLastCrash, activeTimeSinceLaunch: Double
    let applicationActive, applicationInForeground: Bool
    let backgroundTimeSinceLastCrash, backgroundTimeSinceLaunch: Double
    let launchesSinceLastCrash, sessionsSinceLastCrash, sessionsSinceLaunch: Int

    enum CodingKeys: String, CodingKey {
        case activeTimeSinceLastCrash = "active_time_since_last_crash"
        case activeTimeSinceLaunch = "active_time_since_launch"
        case applicationActive = "application_active"
        case applicationInForeground = "application_in_foreground"
        case backgroundTimeSinceLastCrash = "background_time_since_last_crash"
        case backgroundTimeSinceLaunch = "background_time_since_launch"
        case launchesSinceLastCrash = "launches_since_last_crash"
        case sessionsSinceLastCrash = "sessions_since_last_crash"
        case sessionsSinceLaunch = "sessions_since_launch"
    }
}

// MARK: - Memory
public struct Memory: Codable {
    let free, size, usable: Int
}
