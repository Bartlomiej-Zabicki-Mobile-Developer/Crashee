import CrasheeObjc

public final class Crashee {
    
    // MARK: - Enums
    
    public enum SetupType {
        case `default`(token: String)
        case custom(CrasheeSetup)
        
        var setup: CrasheeSetup {
            switch self {
            case .default(let token):
                return CrasheeSetupImpl(token: token)
            case .custom(let customSetup):
                return customSetup
            }
        }
    }
    
    // MARK: - Singleton
    
    public static let shared = Crashee()
    private init() {}
    
    // MARK: - Properties
    
    private var setup: CrasheeSetup?
    
    // MARK: - Functions
    
    public func setup(as setupType: SetupType) {
        self.setup = setupType.setup
    }
    
    public func sendAllReports(completion: @escaping ReportsCompletion) {
        setup?.sendAllReports(completion: completion)
    }
    
    public func deleteAllReports(completion: DeleteReportsCompletion) {
        setup?.deleteAllReports(completion: completion)
    }
    
}
