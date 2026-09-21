// fkey <pid> <token>...   farman のプロセスへキーイベントを直接送る
// token: down up left right return enter space esc backspace tab home end
//        a..z 0..9 (単キー) / cmd+x shift+x alt+x の組合せ / text:<文字列> / sleep:<ms> / activate
import AppKit
import CoreGraphics

let codes: [String: CGKeyCode] = [
  "a":0,"s":1,"d":2,"f":3,"h":4,"g":5,"z":6,"x":7,"c":8,"v":9,"b":11,"q":12,"w":13,"e":14,"r":15,
  "y":16,"t":17,"1":18,"2":19,"3":20,"4":21,"6":22,"5":23,"9":25,"7":26,"8":28,"0":29,
  "o":31,"u":32,"i":34,"p":35,"l":37,"j":38,"k":40,"n":45,"m":46,
  "return":36,"enter":36,"tab":48,"space":49,"backspace":51,"esc":53,
  "left":123,"right":124,"down":125,"up":126,"home":115,"end":119,"slash":44,
]
let args = Array(CommandLine.arguments.dropFirst())
guard args.count >= 2, let pidInt = Int32(args[0]) else { fputs("usage: fkey <pid> <token>...\n", stderr); exit(2) }
let pid = pid_t(pidInt)
let src = CGEventSource(stateID: .hidSystemState)

func post(_ code: CGKeyCode, _ flags: CGEventFlags) {
  for down in [true, false] {
    guard let e = CGEvent(keyboardEventSource: src, virtualKey: code, keyDown: down) else { continue }
    e.flags = flags
    e.postToPid(pid)
    usleep(15_000)
  }
}
func postText(_ s: String) {
  for ch in s {
    let u = Array(String(ch).utf16)
    for down in [true, false] {
      guard let e = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: down) else { continue }
      e.flags = []
      e.keyboardSetUnicodeString(stringLength: u.count, unicodeString: u)
      e.postToPid(pid)
      usleep(12_000)
    }
  }
}
for tok in args.dropFirst() {
  if tok == "activate" {
    NSRunningApplication(processIdentifier: pid)?.activate(options: [.activateAllWindows])
    usleep(400_000); continue
  }
  if tok.hasPrefix("sleep:") { usleep(UInt32(tok.dropFirst(6))! * 1000); continue }
  if tok.hasPrefix("text:") { postText(String(tok.dropFirst(5))); usleep(80_000); continue }
  var flags: CGEventFlags = []
  var key = tok
  let parts = tok.split(separator: "+").map(String.init)
  if parts.count > 1 {
    key = parts.last!
    for m in parts.dropLast() {
      switch m {
      case "cmd": flags.insert(.maskCommand)
      case "shift": flags.insert(.maskShift)
      case "alt": flags.insert(.maskAlternate)
      case "ctrl": flags.insert(.maskControl)
      default: fputs("unknown modifier \(m)\n", stderr); exit(2)
      }
    }
  }
  guard let code = codes[key] else { fputs("unknown key \(key)\n", stderr); exit(2) }
  post(code, flags)
  usleep(120_000)
}
