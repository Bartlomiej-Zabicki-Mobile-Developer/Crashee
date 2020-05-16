// swift-tools-version:5.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "Crashee",
    platforms: [
        .iOS(.v12),
    ],
    products: [
        .library(
            name: "Crashee",
            targets: ["Crashee"]),
    ],
    dependencies: [],
    targets: [
        .target(
            name: "CrasheeObjc",
            dependencies: []),
        .target(
            name: "Crashee",
            dependencies: ["CrasheeObjc"]),
        .testTarget(
            name: "CrasheeTests",
            dependencies: ["Crashee"]),
    ]
)
