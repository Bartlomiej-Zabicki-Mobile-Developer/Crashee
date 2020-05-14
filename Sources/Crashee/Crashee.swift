import CrasheeObjc

public final class Crashee {
    
    // MARK: - Singleton
    
    public static let shared = Crashee()
    private init() {}
    
    // MARK: - Properties
    
    private(set) public var token: String?
    private let setup = CrasheeSetup()
    
    // MARK: - Functions
    
    public func setup(with token: String) {
        self.token = token
        setup.install()
    }
    
    public func sendAllReports(completion: @escaping () -> Void) {
        setup.sendAllReports { (reports, _, error) in
            print("Reports: \(reports?.count)")
            print("Error: \(error)")
            completion()
        }
        
    }
    
}
