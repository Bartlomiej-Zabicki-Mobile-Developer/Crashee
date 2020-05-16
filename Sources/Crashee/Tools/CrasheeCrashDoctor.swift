//
//  CrasheeCrashDoctor.swift
//  
//
//  Created by Bartłomiej Zabicki on 16/05/2020.
//

import Foundation

protocol CrashDoctor: class {
    func diagnoseCrash(report: CrashReport) -> String?
}

final class CrasheeCrashDoctor: CrashDoctor {
    
    func diagnoseCrash(report: CrashReport) -> String? {
        let lastFunctionName = lastInAppStackEntry(in: report)?.symbolName ?? "Unkown"
        let crashedThread = report.crashedThread
        let errorReport = report.errorReport
        
        if report.isDeadlock {
            return "Main thread is deadlocked at \(lastFunctionName)"
        }
        if crashedThread?.isStackOverflow == true {
            return "Stack overflow at \(lastFunctionName)"
        }
        
        if errorReport.isMathError {
            return "Math error usualy caused from division by 0, at \(lastFunctionName)"
        }
        if errorReport.isInvalidAddress {
            if errorReport.address == 0 {
                return "Attempted to reference null pointer at \(lastFunctionName)"
            }
            return "Attempted to reference garbage pointer \(errorReport.address) at \(lastFunctionName)"
        }
        return nil
    }
    
    // MARK: - Private implementation
    
    private func lastInAppStackEntry(in report: CrashReport) -> BacktraceContent? {
        report.crashedContents?.first(where: { $0.objectName == report.processName })
    }
    
}

private extension CrashReport {
    
    var processName: String { report.processName }
    var crashedThread: CrashThread? { crash.threads.first(where: { $0.crashed })}
    var crashedContents: [BacktraceContent]? { crashedThread?.backtrace.contents }
    var errorReport: ErrorCrash { crash.error }
    var isDeadlock: Bool { errorReport.type == "deadlock" }
    
}

private extension CrashThread {
    
    var isStackOverflow: Bool { stack?.overflow == true }
    
}

private extension ErrorCrash {
    
    var isMathError: Bool {
        if let mach = mach {
            return mach.exceptionName == "EXC_ARITHMETIC"
        }
        return signal.name == "SIGFPE"
    }
    
    var isInvalidAddress: Bool {
        if let mach = mach {
            return mach.exceptionName == "EXC_BAD_ACCESS"
        }
        return signal.name == "SIGSEGV"
    }
    
}

extension CrashReport {
    
    mutating func diagnosed(with doctor: CrashDoctor) -> CrashReport {
        crash.diagnosis = doctor.diagnoseCrash(report: self)
        return self
    }
    
}
