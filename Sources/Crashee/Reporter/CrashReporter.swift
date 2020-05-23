//
//  CrashReporter.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import CrasheeObjc
import UIKit

public enum ReportsError: Swift.Error {
    case underlying(Swift.Error)
}

public typealias ReportsCompletion = (Result<[CrashReport], ReportsError>) -> Void
public typealias DeleteReportsCompletion = () -> Void

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
        let folderName: String = "CrasheeCrash"
        return dirURL.appendingPathComponent(folderName)
            .appendingPathComponent(bundleName).path.replacingOccurrences(of: " ", with: "-")
    }()
    private lazy var jsonDecoder: JSONDecoder = .init()
    private(set) var reportHandler: ReportHandler
    private var monitoring: CrasheeCrashMonitorType!
    private lazy var crashDoctor: CrashDoctor = CrasheeCrashDoctor()
    
    // MARK: - Initialization
    
    init(reportHandler: ReportHandler) {
        self.reportHandler = reportHandler
        subscribeForNotifications()
    }
    
    // MARK: - Functions
    
    internal func sendAllReports(with completion: @escaping ReportsCompletion) {
        let reports = allReports()
        guard !reports.isEmpty else {
            completion(.success([]))
            return 
        }
        send(reports: allReports(), completion: completion)
    }
    
    internal func deleteAllReports(with completion: DeleteReportsCompletion) {
        crasheecrash_deleteAllReports()
        completion()
    }
    
    internal func deleteReport(withId reportId: Int) {
        crasheecrash_deleteReportWithID(Int64(reportId))
    }
    
    ///This function notify crasheec about loading but also invokes singleton
    internal func install() {
        monitoring = crasheecrash_install(NSString(string: bundleName).utf8String, NSString(string: basePath).utf8String)
        didLoad()
    }
    
    // MARK: - Private implementation
    
    private func allReports() -> [CrashReport] {
        reportIDs().compactMap({ reportWith(id: $0) })
        
    }
    private func reportIDs() -> [Int] {
        let reportsCount = crasheecrash_getReportCount()
        var reportIDs: [Int64] = .init(repeating: 0, count: Int(reportsCount))
        crasheecrash_getReportIDs(&reportIDs, reportsCount)
        return reportIDs.compactMap({ Int($0) })
    }
    
    private func reportWith(id reportId: Int) -> CrashReport? {
        guard let data = reportDataWith(id: reportId) else { return nil }
        do {
            var report = try jsonDecoder.decode(CrashReport.self, from: data)
            return report.diagnosed(with: crashDoctor)
        } catch {
            print("Decoding error: \(error)")
            return nil
        }
    }
    
    private func reportDataWith(id: Int) -> Data? {
        guard let report = crasheecrash_readReport(Int64(id)) else { return nil }
        return Data(bytesNoCopy: report, count: strlen(report), deallocator: .free)
    }
    
    private func send(reports: [CrashReport], completion: @escaping ReportsCompletion) {
        reportHandler.handle(reports: reports, completion: completion)
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
        crasheecrash_notifyObjCLoad()
    }
    
    @objc private func applicationDidBecomeActive() {
        crasheecrash_notifyAppActive(true)
    }
    
    @objc private func applicationWillResignActive() {
        crasheecrash_notifyAppActive(false)
    }
    
    @objc private func applicationDidEnterBackground() {
        crasheecrash_notifyAppInForeground(false)
    }
    
    @objc private func applicationWillEnterForeground() {
        crasheecrash_notifyAppInForeground(true)
    }
    
    @objc private func applicationWillTerminate() {
        crasheecrash_notifyAppTerminate()
    }
    
}
