//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import CrasheeObjc

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
    var userCrashCallback: KSReportWriteCallback?
    var reportFieldsCount: Int
    var reportFields: [ReportField]
    
    init(reportFieldsCount: Int, reportFields: [ReportField]) {
        self.reportFieldsCount = reportFieldsCount
        self.reportFields = reportFields
        userCrashCallback = nil
    }
}

func crashCallback(writer: inout KSCrashReportWriter) -> KSCrashReportWriter {
    for reportField in CrasheeSetup.g_crashHandlerData.reportFields {
        writer.addJSONElement(&writer, reportField.key, reportField.value, true)
    }
    if let callback = CrasheeSetup.g_crashHandlerData.userCrashCallback {
        callback(&writer)
    }
    return writer
}

protocol CrasheeConverter {}

typealias DeleteCompletion = () -> Void

class CrashReportField {
    let index: Int
    var field: ReportField?
    var key: String?
    var value: String?
    var fieldBacking: Data?
    var keyBacking: KSCString?
    var valueBacking: KSCString?
    
    init(index: Int) {
        self.index = index
    }
    
    func set(key: String) {
        self.key = key
        keyBacking = KSCString(string: key)
        field?.change(key: key)
    }
    
    func set(value: String) {
        var error: NSError
        do {
            let jsonData = try KSJSONCodec.encode(value, options: KSJSONEncodeOptionPretty )
            self.value = value
            self.valueBacking = KSCString(data: jsonData)
            self.field?.change(value: value)
        } catch {
            print("Could not set value %@ for property %@: %@", value, self.key, error)
        }
        
    }
}

protocol BasicSetup {
    var onCrash: KSReportWriteCallback? { get  set }
    func converter() -> CrasheeConverter
    func sendAllReports(completion: KSCrashReportFilterCompletion?)
    func deleteAllReports(completion: DeleteCompletion)
    func addPreFilter(_ filter: KSCrashReportFilter?)
}

class CrasheeSetup: BasicSetup {
    
    static var g_crashHandlerData: CrashHandlerData = .init(reportFieldsCount: 0, reportFields: [])
    
    
    var nextFieldIndex: Int = 0
    var crashHandlerDataBacking: NSMutableData?
    var crashHandlerData: CrashHandlerData?
    var fields: [String: CrashReportField] = [:]
    var prependedFilters: KSCrashReportFilterPipeline = KSCrashReportFilterPipeline()
    
    var onCrash: KSReportWriteCallback? {
        didSet {
            crashHandlerData?.userCrashCallback = onCrash
        }
    }
    
    // MARK: - Initialization
    
    init() {}
    
    deinit {
//        let handler = KSCrash.sharedInstance()
//        if Self.g_crashHandlerData == crashHandlerData() {
//            Self.g_crashHandlerData = nil
//            handler?.onCrash = nil
//        }
    }
    
    // MARK: - Functions
    
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
    
    func install() {
        var handler = KSCrash.sharedInstance()
//        Self.g_crashHandlerData = crashHandlerData!
        handler?.onCrash = onCrash
        handler?.install()
    }
    
    // MARK: - BasicSetup
    
    func converter() -> CrasheeConverter {
        return JSONConverter()
    }
    
    func sendAllReports(completion: KSCrashReportFilterCompletion?) {
        let sink = KSCrashReportFilterPipeline()
        var handler = KSCrash.sharedInstance()
        handler?.sink = sink
        handler?.sendAllReports(completion: completion)
    }
    
    func deleteAllReports(completion: DeleteCompletion) {
        
    }
    
    func addPreFilter(_ filter: KSCrashReportFilter?) {
        
    }
    
    
}
