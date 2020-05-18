//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation

public final class APISetup: CrasheeSetup {
    
    public struct EndpointConfiguration {
        let url: URL
        let method: Networking.NetworkMethod
        var headers: [String: String]
        var urlSession: URLSession
        
        public init(url: URL,
                    method: Networking.NetworkMethod,
                    headers: [String: String] = [:],
                    urlSession: URLSession = URLSession(configuration: .default)) {
            self.url = url
            self.method = method
            self.headers = headers
            self.urlSession = urlSession
        }
    }
    
    private lazy var bundleName: String = (Bundle.main.infoDictionary?["CFBundleName"] ?? "Unknown") as! String
    private lazy var apiHandler = APIReportHandler(url: configuration.url,
                                                   configuration: .init(headers: configuration.headers,
                                                                        method: configuration.method,
                                                                        session: configuration.urlSession))
    private lazy var crashReporter: CrashReporter = CrashReporter.init(reportHandler: apiHandler)
    private(set) public var configuration: EndpointConfiguration
    
    // MARK: - Initialization
    
    public init(configuration: EndpointConfiguration) {
        self.configuration = configuration
    }
    
    // MARK: - CrasheeSetup
    
    public func sendAllReports(completion: @escaping ReportsCompletion) {
        crashReporter.sendAllReports(with: completion)
    }
    
    public func deleteAllReports(completion: DeleteReportsCompletion) {
        crashReporter.deleteAllReports(with: completion)
    }
    
}
