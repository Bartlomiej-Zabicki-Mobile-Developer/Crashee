//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation

struct ReportField {
    var key: String
    var value: String
    
    mutating func change(key: String) {
        self.key = key
    }
    mutating func change(value: String) {
        self.value = value
    }
}

class CrashHandlerData {
//    var userCrashCallback: KSReportWriteCallback?
    var reportFieldsCount: Int
    var reportFields: [ReportField]
    
    init(reportFieldsCount: Int, reportFields: [ReportField]) {
        self.reportFieldsCount = reportFieldsCount
        self.reportFields = reportFields
//        userCrashCallback = nil
    }
}

//func crashCallback(writer: inout KSCrashReportWriter) -> KSCrashReportWriter {
//    for reportField in CrasheeSetup.g_crashHandlerData.reportFields {
//        writer.addJSONElement(&writer, reportField.key, reportField.value, true)
//    }
//    if let callback = CrasheeSetup.g_crashHandlerData.userCrashCallback {
//        callback(&writer)
//    }
//    return writer
//}

protocol CrasheeConverter {}

typealias DeleteCompletion = () -> Void

class CrashReportField {
    let index: Int
    var field: ReportField?
    var key: String?
    var value: String?
    var fieldBacking: Data?
    var keyBacking: String?
    var valueBacking: String?
    
    init(index: Int) {
        self.index = index
    }
    
    func set(key: String) {
        self.key = key
        keyBacking = key
        field?.change(key: key)
    }
    
    func set(value: String) {
        var error: NSError
        do {
//            let jsonData = try KSJSONCodec.encode(value, options: KSJSONEncodeOptionPretty )
            self.value = value
//            self.valueBacking = String(data: jsonData, encoding: .utf8)
            self.field?.change(value: value)
        } catch {
            print("Could not set value %@ for property %@: %@", value, self.key, error)
        }
        
    }
}

protocol BasicSetup {
    func converter() -> CrasheeConverter
    func sendAllReports(completion: @escaping ReportsCompletion)
    func deleteAllReports(completion: DeleteCompletion)
}

class CrasheeSetup: BasicSetup {
    
    
    var nextFieldIndex: Int = 0
    var crashHandlerDataBacking: NSMutableData?
    var crashHandlerData: CrashHandlerData?
    var fields: [String: CrashReportField] = [:]
    private lazy var bundleName: String = (Bundle.main.infoDictionary?["CFBundleName"] ?? "Unknown") as! String
    private var token: String = ""
    private lazy var urlSession = URLSession(configuration: .default)
    private lazy var apiHandler = APIReportHandler(url: URL(string: "https://c89fa463.ngrok.io/crashreport")!,
                                                   configuration: .init(headers: ["organizationToken": token,
                                                                                  "User-Agent": "Crashee",
                                                                                  "bundleName": bundleName,
                                                                                  "bundleId": Bundle.main.bundleIdentifier ?? ""],
                                                                        method: .post,
                                                                        session: urlSession))
    private lazy var crashReporter: CrashReporter = CrashReporter.init(reportHandler: apiHandler)
    
    // MARK: - Functions
    
    func changeToken(to token: String) {
        self.token = token
    }
    
    func reportFieldFor(property: String) -> CrashReportField {
        guard fields[property] == nil else { return fields[property]! }
        let field = CrashReportField(index: nextFieldIndex)
        nextFieldIndex += 1
        crashHandlerData?.reportFieldsCount = nextFieldIndex
        crashHandlerData?.reportFields.append(field.field!)
        fields[property] = field
        return field
    }
    
    func set(key: String, forPropert property: String) {
        let field = reportFieldFor(property: property)
        field.key = key
    }
    
    func set(value: String, forPropert property: String) {
        let field = reportFieldFor(property: property)
        field.value = value
    }
    
    // MARK: - BasicSetup
    
    func converter() -> CrasheeConverter {
        return JSONConverter()
    }
    
    func sendAllReports(completion: @escaping ReportsCompletion) {
        crashReporter.sendAllReports(with: completion)
    }
    
    func deleteAllReports(completion: DeleteReportsCompletion) {
        crashReporter.deleteAllReports(with: completion)
    }
    
    
}
