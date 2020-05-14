//
//  File.swift
//  
//
//  Created by Bartłomiej Zabicki on 14/05/2020.
//

import Foundation

struct JSONConverter: CrasheeConverter {
    
}

final class APISetup: CrasheeSetup {
    
    override func converter() -> CrasheeConverter {
        return JSONConverter()
    }
    
}
