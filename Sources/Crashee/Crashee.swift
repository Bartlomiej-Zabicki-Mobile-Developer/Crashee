import CrasheeObjc

public struct TestWrapper {
    private(set) public var token = "Hello, World!"
    
    public func test() {
//        Foo()
//        let foo = Foo()
//        Foo()
        let x = Foo().foo()
        print("X foo: \(x)")
    }
    
    public init(token: String) {
        self.token = token
    }
    
}

public final class Crashee: NSObject {
    
    public static let shared = Crashee()
    private override init() {}
    public let webInstallation = KSCrashInstallationStandard.sharedInstance
    
    public func setup(token: String) {
        
    }
    
}
