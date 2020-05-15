//
//  Networking.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation

typealias JSON = [String: Any]
typealias Headers = [String: String]
typealias NetworkingError = Networking.NetworkError
typealias DataTaskResult = (Data?, URLResponse?, Error?) -> Void

// MARK: - Networking

final public class Networking {
    
    // MARK: - Properties
    
    public var session: URLSession
    public var timeoutIntervalForRequest = 15.0
    public var shouldUseMultipartAutomatically = true
    public var shouldPrintDebugErrors = true
    
    // MARK: - Initialization
    
    init(session: URLSession) {
        self.session = session
    }
    
    // MARK: - Enums
    
    public enum NetworkMethod: String {
        case post = "POST"
        case get = "GET"
        case delete = "DELETE"
        case put = "PUT"
    }
    
    public enum Encoding: String {
        case json = "application/json"
        case url = "application/x-www-form-urlencoded"
    }
    
    // MARK: - Error
    
    public struct NetworkError: Swift.Error, LocalizedError {
        
        public let code: Int
        public let description: String
        
        init(code: Int, description: String) {
            self.description = description
            self.code = code
        }
        
        init(code: Int, data: Data) {
            var message = "Expecting JSON as result"
            if let json = ((try? JSONSerialization.jsonObject(with: data, options: []) as? JSON) as JSON??) {
                if let description = json?["reason"] as? String {
                    message = description
                } else if let key = json?.keys.first, let description = (json?[key] as? [String])?.first {
                    message = description
                } else if let decodedMessage = String(data: data, encoding: .utf8) {
                    message = decodedMessage
                }
            }
            self.init(code: code, description: message)
        }
        
    }
    
    // MARK: - Public functions
    
    func request(with url: URL?, method: NetworkMethod, parameters: JSON? = nil,
                 reports: (key: String, reportsData: [Data])?,
                        headers: JSON? = nil, encoding: Encoding = .json, onSuccess: @escaping ((Data) -> Void),
                        onError: @escaping ((NetworkError) -> Void)) {
        guard let url = url else {
            print("Cannot run request without url")
            return
        }
        var request = URLRequest(url: url)
        request.httpMethod = method.rawValue
        headers?.forEach { element in
            if let value = element.value as? String {
                request.addValue(value, forHTTPHeaderField: element.key)
            }
        }
        switch method {
        case .post, .put:
            if let reports = reports {
                request = setupMultipart(request: request,
                                         with: parameters,
                                         reports: reports)
            } else {
                if let setupedRequest = setupRequestWith(request: request, parameters: parameters, headers: headers, encoding: encoding) {
                    request = setupedRequest
                }
            }
        default:break
        }
        
        _ = session.dataTask(with: request) { [weak self] (data, response, error) in
            guard let self = self else { return }
            guard let data = data, let statusCode = (response as? HTTPURLResponse)?.statusCode else {
                let error = NetworkError(code: (response as? HTTPURLResponse)?.statusCode ?? 0,
                                         description: error?.localizedDescription ?? "No error")
                onError(error)
                return
            }
            
            switch statusCode {
            case 200...299:
                onSuccess(data)
            case 400...600:
                if self.shouldPrintDebugErrors {
                    debugPrint(request)
                }
                onError(NetworkError(code: statusCode, data: data))
            default: break
            }
        }.resume()
    }
    
    // MARK: - Private functions
    
    private func defaultSession() -> URLSession {
        let sessionConfiguration = URLSessionConfiguration.default
        sessionConfiguration.timeoutIntervalForRequest = timeoutIntervalForRequest
        let session = URLSession(configuration: sessionConfiguration, delegate: nil, delegateQueue: OperationQueue.main)
        return session
    }
    
    private func setupRequestWith(request: URLRequest, parameters: JSON? = nil, headers: JSON?, encoding: Encoding) -> URLRequest? {
        var request = request
        request.addValue(encoding.rawValue, forHTTPHeaderField: "Content-Type")
        request.addValue("application/json", forHTTPHeaderField: "Accept")
        guard let parameters = parameters else { return request }
        do {
            request.httpBody = try JSONSerialization.data(withJSONObject: parameters, options: .prettyPrinted)
        } catch {
            print("Cannot parse parameters into JSON")
            return nil
        }
        return request
    }
    
    private func setupMultipart(request: URLRequest, with parameters: JSON?,
                                reports: (key: String, reportsData: [Data])) -> URLRequest {
        
        let boundary = "Boundary-\(UUID().uuidString)"
        var request = request
        request.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        let body = NSMutableData()
        let boundaryPrefix = "\r\n--\(boundary)\r\n"
        
        reports.reportsData.enumerated().forEach { index, reportData in
            body.appendString(boundaryPrefix)
            body.appendString("Content-Disposition:form-data; name=\"\(reports.key)\"; filename=\"report\(index + 1).json\"\r\n")
            body.appendString("Content-Type: json\r\n\r\n")
            body.append(reportData)
        }
        body.appendString(boundaryPrefix)
        parameters?.forEach({ (key, value) in
            body.appendString(boundaryPrefix)
            body.appendString("Content-Disposition: form-data; name=\"\(key)\"\r\n\r\n")
            body.appendString("\(value)\r\n")
        })
        
        body.appendString("--".appending(boundary.appending("--")))
        request.httpBody = body as Data
        return request
    }
    
}

// MARK: - NSMutableData Extension - Specific to this Networking wrapper

extension NSMutableData {
    
    public func appendString(_ string: String) {
        let data = string.data(using: String.Encoding.utf8, allowLossyConversion: false)
        append(data!)
    }
    
}

