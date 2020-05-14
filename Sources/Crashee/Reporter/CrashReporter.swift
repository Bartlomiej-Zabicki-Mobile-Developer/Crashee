//
//  CrashReporter.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import CrasheeObjc
import UIKit

final class CrashReporter {
    
    // MARK: - Properties
    
    public lazy var bundleName: String = (Bundle.main.infoDictionary?["CFBundleName"] ?? "Unknown") as! String
    public lazy var basePath: String = {
        guard var path = NSSearchPathForDirectoriesInDomains(.cachesDirectory, .userDomainMask, true).first else {
            print("Could locate base path")
            return ""
        }
        let dirURL = URL(fileURLWithPath: path, isDirectory: true)
        //TODO: - Change to CrasheeReports
        let folderName: String = "KSCrash"
        return dirURL.appendingPathComponent(folderName)
            .appendingPathComponent(bundleName).path.replacingOccurrences(of: " ", with: "-")
    }()
    private lazy var jsonDecoder: JSONDecoder = .init()
    private(set) var reportHandler: ReportHandler
    private(set) var onCrash: KSReportWriteCallback? {
        didSet {
            kscrash_setCrashNotifyCallback(onCrash)
        }
    }
    private var monitoring: KSCrashMonitorType!
    
    // MARK: - Initialization
    
    init(reportHandler: ReportHandler, crashCompletion: inout KSReportWriteCallback?) {
        self.reportHandler = reportHandler
        self.onCrash = crashCompletion
        subscribeForNotifications()
        monitoring = kscrash_install(NSString(string: bundleName).utf8String,
        NSString(string: basePath).utf8String)
    }
    
    // MARK: - Functions
    
    internal func sendAllReports(with completion: KSCrashReportFilterCompletion?) {
        send(reports: allReports()) { (filteredReports, completed, error) in
            kscrash_callCompletion(completion, filteredReports, completed, error)
        }
    }
    
    internal func deleteAllReports() {
        kscrash_deleteAllReports()
    }
    
    internal func deleteReport(withId reportId: Int) {
        kscrash_deleteReportWithID(Int64(reportId))
    }
    
    // MARK: - Private implementation
    
    
    private func allReports() -> [CrashReport] {
        reportIDs().compactMap({ reportWith(id: $0) })
        
    }
    private func reportIDs() -> [Int] {
        let reportsCount = kscrash_getReportCount()
        var reportIDs: [Int64] = .init(repeating: 0, count: Int(reportsCount))
        kscrash_getReportIDs(&reportIDs, reportsCount)
        return reportIDs.compactMap({ Int($0) })
    }
    
    private func reportWith(id reportId: Int) -> CrashReport? {
        guard let data = reportDataWith(id: reportId) else { return nil }
        do {
            return try jsonDecoder.decode(CrashReport.self, from: data)
        } catch {
            print("Decoding error: \(error)")
            return nil
        }
    }
    
    private func reportDataWith(id: Int) -> Data? {
        guard let report = kscrash_readReport(Int64(id)) else { return nil }
        return Data(bytesNoCopy: report, count: strlen(report), deallocator: .free)
    }
    
    private func send(reports: [CrashReport], completion: @escaping  KSCrashReportFilterCompletion) {
        guard !reports.isEmpty else {
            kscrash_callCompletion(completion, reports, true, nil)
            return
        }
        reportHandler.handle(reports: reports) { (filteredReports, completed, error) in
        kscrash_callCompletion(completion, filteredReports, completed, error)
        }
    }
    
    private func subscribeForNotifications() {
        let notificationCenter = NotificationCenter.default
        notificationCenter.addObserver(self, selector: #selector(applicationDidBecomeActive), name: UIApplication.didBecomeActiveNotification, object: nil)
        notificationCenter.addObserver(self, selector: #selector(applicationWillResignActive), name: UIApplication.willResignActiveNotification, object: nil)
        notificationCenter.addObserver(self, selector: #selector(applicationDidEnterBackground), name: UIApplication.didEnterBackgroundNotification, object: nil)
        notificationCenter.addObserver(self, selector: #selector(applicationWillEnterForeground), name: UIApplication.willEnterForegroundNotification, object: nil)
        notificationCenter.addObserver(self, selector: #selector(applicationWillTerminate), name: UIApplication.willTerminateNotification, object: nil)
    }
    
    // MARK: - Notification Actions
    
    private func didLoad() {
        kscrash_notifyObjCLoad()
    }
    
    @objc private func applicationDidBecomeActive() {
        kscrash_notifyAppActive(true)
    }
    
    @objc private func applicationWillResignActive() {
        kscrash_notifyAppActive(false)
    }
    
    @objc private func applicationDidEnterBackground() {
        kscrash_notifyAppInForeground(false)
    }
    
    @objc private func applicationWillEnterForeground() {
        kscrash_notifyAppInForeground(true)
    }
    
    @objc private func applicationWillTerminate() {
        kscrash_notifyAppTerminate()
    }
    
}
