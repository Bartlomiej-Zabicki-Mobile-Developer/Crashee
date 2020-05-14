//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import CrasheeObjc

protocol ReportHandler {
    func handle(reports: [CrashReport], onCompletion: @escaping KSCrashReportFilterCompletion)
}

final class APIReportHandler: ReportHandler {
    
    struct Configuration {
        let headers: [String: String]
        let method: String
    }
    
    private(set) var url: URL
    private(set) var configuration: Configuration
    
    // MARK: - Initialization
    
    init(url: URL, configuration: Configuration) {
        self.url = url
        self.configuration = configuration
    }
    
    // MARK: - Functions
    
    public func handle(reports: [CrashReport], onCompletion: @escaping KSCrashReportFilterCompletion) {
        // TODO - Handle encoded reports
//        filter?.filterReports(reports, onCompletion: { (filteredReports, completed, error) in
//            if let error = error {
//                print("Error")
//                onCompletion(nil, false, error)
//            }
//            print("Reports: \(filteredReports?.count)")
//            // TODO: - Sending multiparams jsons
//            reports.forEach { report in
//                let jsonData = try? KSJSONCodec.encode(report, options: KSJSONEncodeOptionSorted)
//            }
//            print("Should handle reports")
//            onCompletion(filteredReports, true, nil)
//        })
    }
    
    
}
