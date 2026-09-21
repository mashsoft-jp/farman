// axwin <pid> list | axwin <pid> set <x> <y> <w> <h>   (set はフォーカス中のウィンドウが対象)
import ApplicationServices
import Foundation
let a = CommandLine.arguments
guard a.count >= 3, let p = Int32(a[1]) else { exit(2) }
let app = AXUIElementCreateApplication(pid_t(p))
func attr<T>(_ e: AXUIElement, _ n: String) -> T? { var v: CFTypeRef?; AXUIElementCopyAttributeValue(e, n as CFString, &v); return v as? T }
if a[2] == "list" {
  let wins: [AXUIElement] = attr(app, kAXWindowsAttribute) ?? []
  for w in wins {
    let t: String = attr(w, kAXTitleAttribute) ?? ""
    var pos = CGPoint.zero, size = CGSize.zero
    if let pv: AXValue = attr(w, kAXPositionAttribute) { AXValueGetValue(pv, .cgPoint, &pos) }
    if let sv: AXValue = attr(w, kAXSizeAttribute) { AXValueGetValue(sv, .cgSize, &size) }
    print("\(t)\t\(Int(pos.x)),\(Int(pos.y)),\(Int(size.width)),\(Int(size.height))")
  }
} else if a[2] == "set", a.count == 7 {
  guard let w: AXUIElement = attr(app, kAXFocusedWindowAttribute) else { fputs("no focused window\n", stderr); exit(1) }
  var pos = CGPoint(x: Double(a[3])!, y: Double(a[4])!), size = CGSize(width: Double(a[5])!, height: Double(a[6])!)
  AXUIElementSetAttributeValue(w, kAXSizeAttribute as CFString, AXValueCreate(.cgSize, &size)!)
  AXUIElementSetAttributeValue(w, kAXPositionAttribute as CFString, AXValueCreate(.cgPoint, &pos)!)
  AXUIElementSetAttributeValue(w, kAXSizeAttribute as CFString, AXValueCreate(.cgSize, &size)!)
}
