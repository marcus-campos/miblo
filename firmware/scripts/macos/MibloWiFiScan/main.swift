// MibloWiFiScan: a tiny background app that lists nearby Wi-Fi networks with their real names.
//
// macOS hides SSIDs ("<redacted>") from processes without Location Services access, and
// command-line tools (Terminal, Python) cannot be granted it. An app bundle can: macOS asks
// "MibloWiFiScan would like to use your location" once, and remembers the answer.
//
// flash-fleet.py builds this into firmware/.cache/MibloWiFiScan.app and runs it with
//   open -W -n MibloWiFiScan.app --args <out.json>
// (the output path can also come from $MIBLO_WIFISCAN_OUT). It writes:
//   {"authorized": true, "status": "authorized", "interface": "en0",
//    "current_ssid": "HomeNet", "current_bssid": "aa:bb:...",
//    "networks": [{"ssid": "GIFTV", "bssid": "5e:cf:7f:12:4f:2a", "rssi": -48, "channel": 6}, ...],
//    "error": null}
// and exits. When location access is not granted, "authorized" is false (names are null).
//
// Join mode: `--args <out.json> --join <bssid> [--password <pw>]` associates with that exact
// access point (many stock units share one network name, so joining by name picks any of them)
// and writes {"authorized": ..., "joined": true|false, "error": ...}.

import AppKit
import CoreLocation
import CoreWLAN
import Foundation

let environment = ProcessInfo.processInfo.environment

let arguments = Array(CommandLine.arguments.dropFirst().filter { !$0.hasPrefix("-psn_") })

func option(_ name: String) -> String? {
    guard let i = arguments.firstIndex(of: name), i + 1 < arguments.count else { return nil }
    return arguments[i + 1]
}

let joinBssid = option("--join")?.lowercased()
let joinPassword = option("--password")

let outputPath: String = {
    if let first = arguments.first, !first.isEmpty, !first.hasPrefix("--") { return first }
    if let env = environment["MIBLO_WIFISCAN_OUT"], !env.isEmpty { return env }
    return (NSTemporaryDirectory() as NSString).appendingPathComponent("miblo-wifiscan.json")
}()

// Seconds to wait for the user to answer the location prompt.
let authTimeout: TimeInterval = Double(environment["MIBLO_WIFISCAN_AUTH_TIMEOUT"] ?? "") ?? 60

func statusName(_ s: CLAuthorizationStatus) -> String {
    switch s.rawValue {
    case 0: return "notDetermined"
    case 1: return "restricted"
    case 2: return "denied"
    case 3: return "authorized"
    case 4: return "authorizedWhenInUse"
    default: return "unknown(\(s.rawValue))"
    }
}

func isAuthorized(_ s: CLAuthorizationStatus) -> Bool {
    return s.rawValue == 3 || s.rawValue == 4  // authorizedAlways / authorizedWhenInUse
}

func jsonValue(_ s: String?) -> Any { return s.map { $0 as Any } ?? NSNull() }

final class Scanner: NSObject, NSApplicationDelegate, CLLocationManagerDelegate {
    let manager = CLLocationManager()
    var done = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        manager.delegate = self
        if manager.authorizationStatus == .notDetermined {
            manager.requestWhenInUseAuthorization()
            manager.startUpdatingLocation()  // some macOS versions only prompt once updates start
            DispatchQueue.main.asyncAfter(deadline: .now() + authTimeout) { self.finish() }
        } else {
            finish()
        }
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        if manager.authorizationStatus != .notDetermined { finish() }
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {}
    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {}

    func finish() {
        if done { return }
        done = true
        manager.stopUpdatingLocation()
        let status = manager.authorizationStatus
        var doc: [String: Any] = [
            "authorized": isAuthorized(status),
            "status": statusName(status),
            "networks": [Any](),
            "error": NSNull(),
        ]
        if let bssid = joinBssid {
            doc["joined"] = false
            if let iface = CWWiFiClient.shared().interface() {
                do {
                    let found = try iface.scanForNetworks(withName: nil)
                    if let net = found.first(where: { $0.bssid?.lowercased() == bssid }) {
                        try iface.associate(to: net, password: joinPassword)
                        doc["joined"] = true
                    } else {
                        doc["error"] = "access point \(bssid) not found"
                    }
                } catch {
                    doc["error"] = "join failed: \(error.localizedDescription)"
                }
            } else {
                doc["error"] = "no Wi-Fi interface"
            }
        } else if let iface = CWWiFiClient.shared().interface() {
            doc["interface"] = jsonValue(iface.interfaceName)
            doc["current_ssid"] = jsonValue(iface.ssid())
            doc["current_bssid"] = jsonValue(iface.bssid())
            do {
                let found = try iface.scanForNetworks(withName: nil)
                doc["networks"] = found
                    .sorted { $0.rssiValue > $1.rssiValue }
                    .map { n -> [String: Any] in
                        [
                            "ssid": jsonValue(n.ssid),
                            "bssid": jsonValue(n.bssid),
                            "rssi": n.rssiValue,
                            "channel": n.wlanChannel.map { $0.channelNumber as Any } ?? NSNull(),
                        ]
                    }
            } catch {
                doc["error"] = "scan failed: \(error.localizedDescription)"
            }
        } else {
            doc["error"] = "no Wi-Fi interface"
        }
        do {
            let data = try JSONSerialization.data(withJSONObject: doc, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: URL(fileURLWithPath: outputPath), options: .atomic)
        } catch {
            FileHandle.standardError.write("MibloWiFiScan: cannot write \(outputPath): \(error)\n".data(using: .utf8)!)
            exit(1)
        }
        exit(0)
    }
}

let app = NSApplication.shared
let scanner = Scanner()
app.delegate = scanner
app.setActivationPolicy(.accessory)
app.run()
