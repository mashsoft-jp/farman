import CoreGraphics
import Foundation
let pidArg = CommandLine.arguments.count > 1 ? Int(CommandLine.arguments[1]) : nil
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as! [[String: Any]]
for w in list {
  guard let owner = w[kCGWindowOwnerName as String] as? String, owner == "farman" else { continue }
  let pid = w[kCGWindowOwnerPID as String] as? Int ?? 0
  if let p = pidArg, p != pid { continue }
  let id = w[kCGWindowNumber as String] as? Int ?? 0
  let layer = w[kCGWindowLayer as String] as? Int ?? 0
  let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
  let name = w[kCGWindowName as String] as? String ?? ""
  print("\(id)\t\(pid)\t\(layer)\t\(b["X"] ?? 0),\(b["Y"] ?? 0),\(b["Width"] ?? 0),\(b["Height"] ?? 0)\t\(name)")
}
