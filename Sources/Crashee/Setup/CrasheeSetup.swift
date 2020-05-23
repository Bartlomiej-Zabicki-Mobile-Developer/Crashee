//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation


public protocol CrasheeSetup: class {
    func sendAllReports(completion: @escaping ReportsCompletion)
    func deleteAllReports(completion: DeleteReportsCompletion)
}

typealias DeleteCompletion = () -> Void

public final class CrasheeSetupImpl: CrasheeSetup {
    
    private(set) public var token: String
    private lazy var bundleName: String = (Bundle.main.infoDictionary?["CFBundleName"] ?? "Unknown") as! String
    private lazy var urlSession = URLSession(configuration: .default)
    private lazy var apiHandler = APIReportHandler(url: URL(string: "https://5fbae2a9.ngrok.io/crashreport")!,
                                                   configuration: .init(headers: ["organizationToken": token,
                                                                                  "User-Agent": "Crashee",
                                                                                  "bundleName": bundleName,
                                                                                  "bundleId": Bundle.main.bundleIdentifier ?? ""],
                                                                        method: .post,
                                                                        session: urlSession))
    private lazy var crashReporter: CrashReporter = CrashReporter.init(reportHandler: apiHandler)
    
    // MARK: - Initialization
    
    public init(token: String) {
        self.token = token
        crashReporter.install()
    }
    
    // MARK: - Functions
    
    func changeToken(to token: String) {
        self.token = token
    }
    
    // MARK: - CrasheeSetup
    
    public func sendAllReports(completion: @escaping ReportsCompletion) {
        crashReporter.sendAllReports(with: completion)
    }
    
    public func deleteAllReports(completion: DeleteReportsCompletion) {
        crashReporter.deleteAllReports(with: completion)
    }
    
}
