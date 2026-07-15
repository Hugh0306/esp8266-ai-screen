import Foundation

// Talks to the ESP8266 clock's own HTTP API, so everything the device's web
// page can do (switch display, set bridge host, upload/reset pet GIFs) is
// available straight from the menu bar. Device address persists in defaults.

struct DeviceInfo {
    var ip = ""
    var ssid = ""
    var bridge = ""
    var mode = "auto"       // configured: auto | claude | codex | net | weather | stock
    var clockTheme = "classic"
    var effective = "auto"  // what's actually on screen
    var showing = ""
    var lastUpdateS = -1    // seconds since the device last got /status data, -1 = never
    var spriteRev = 0       // bumped by the device on animation change
    var brightness = 100    // backlight 0-100 (0 = off)
    var claudeCustomSprite = false
    var codexCustomSprite = false
    var claudeW = 111, claudeH = 120
    var codexW = 120, codexH = 120
}

final class DeviceClient {
    enum AutoPairResult {
        case paired(String)
        case notFound
        case cancelled
    }

    private static let hostKey = "device_host"
    private static let lastSeenKey = "device_last_seen"
    private static let deviceIDKey = "device_id"
    private static var pollVerifications = Set<String>()
    private static var pairingGeneration = 0
    private static var verifiedDeviceIP = ""
    private static var verifiedDeviceID = ""

    static var host: String {
        get { UserDefaults.standard.string(forKey: hostKey) ?? "" }
        set {
            UserDefaults.standard.set(newValue, forKey: hostKey)
            UserDefaults.standard.removeObject(forKey: deviceIDKey)
            verifiedDeviceIP = ""
            verifiedDeviceID = ""
            pairingGeneration &+= 1
        }
    }

    /// Last LAN address that polled our /status — i.e. the clock itself.
    static var lastSeenIP: String {
        get { UserDefaults.standard.string(forKey: lastSeenKey) ?? "" }
        set { UserDefaults.standard.set(newValue, forKey: lastSeenKey) }
    }

    static var baseURL: URL? {
        let h = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !h.isEmpty else { return nil }
        return URL(string: h.hasPrefix("http") ? h : "http://\(h)")
    }

    /// GET /api/info
    static func fetchInfo(completion: @escaping (Result<DeviceInfo, Error>) -> Void) {
        guard let base = baseURL else {
            completion(.failure(Self.noHostError))
            return
        }
        var req = URLRequest(url: base.appendingPathComponent("api/info"))
        req.timeoutInterval = 5
        URLSession.shared.dataTask(with: req) { data, _, error in
            var result: Result<DeviceInfo, Error>
            if let error = error {
                result = .failure(error)
            } else if let data = data,
                      let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                var info = DeviceInfo()
                info.ip = obj["ip"] as? String ?? ""
                info.ssid = obj["ssid"] as? String ?? ""
                info.bridge = obj["bridge"] as? String ?? ""
                info.mode = obj["mode"] as? String ?? "auto"
                info.clockTheme = obj["clock_theme"] as? String ?? "classic"
                info.effective = obj["effective"] as? String ?? info.mode
                info.showing = obj["showing"] as? String ?? ""
                info.lastUpdateS = (obj["last_update_s"] as? NSNumber)?.intValue ?? -1
                info.spriteRev = (obj["sprite_rev"] as? NSNumber)?.intValue ?? 0
                info.brightness = (obj["brightness"] as? NSNumber)?.intValue ?? 100
                let claude = obj["claude"] as? [String: Any]
                let codex = obj["codex"] as? [String: Any]
                info.claudeCustomSprite = claude?["custom_sprite"] as? Bool ?? false
                info.codexCustomSprite = codex?["custom_sprite"] as? Bool ?? false
                info.claudeW = (claude?["w"] as? NSNumber)?.intValue ?? 111
                info.claudeH = (claude?["h"] as? NSNumber)?.intValue ?? 120
                info.codexW = (codex?["w"] as? NSNumber)?.intValue ?? 120
                info.codexH = (codex?["h"] as? NSNumber)?.intValue ?? 120
                result = .success(info)
            } else {
                result = .failure(Self.badResponseError)
            }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }

    /// POST /api/display  mode=auto|claude|codex|net|weather|stock
    static func setDisplayMode(_ mode: String, completion: @escaping (Error?) -> Void) {
        postForm(path: "api/display", fields: ["mode": mode], completion: completion)
    }

    /// POST /api/bridge  host=ip:port
    static func setBridgeHost(_ bridgeHost: String, completion: @escaping (Error?) -> Void) {
        postForm(path: "api/bridge", fields: ["host": bridgeHost], completion: completion)
    }

    /// POST /api/brightness  level=0-100 (0 = backlight off); device persists it
    static func setBrightness(_ level: Int, completion: @escaping (Error?) -> Void) {
        postForm(path: "api/brightness", fields: ["level": String(level)], completion: completion)
    }

    /// POST /sprite/{claude|codex}  multipart GIF upload — the device decodes
    /// and rescales the GIF on-board, then swaps the animation immediately.
    static func uploadGif(_ gif: Data, slot: String, completion: @escaping (Error?) -> Void) {
        guard let base = baseURL else {
            completion(Self.noHostError)
            return
        }
        var req = URLRequest(url: base.appendingPathComponent("sprite/\(slot)"))
        req.httpMethod = "POST"
        req.timeoutInterval = 60 // on-device decode takes a few seconds
        let boundary = "aiclock-\(UUID().uuidString)"
        req.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        var body = Data()
        body.append(Data("--\(boundary)\r\n".utf8))
        body.append(Data("Content-Disposition: form-data; name=\"file\"; filename=\"pet.gif\"\r\n".utf8))
        body.append(Data("Content-Type: image/gif\r\n\r\n".utf8))
        body.append(gif)
        body.append(Data("\r\n--\(boundary)--\r\n".utf8))
        req.httpBody = body
        run(req, completion: completion)
    }

    /// POST /sprite/{claude|codex}/reset — back to the compiled-in animation.
    static func resetSprite(slot: String, completion: @escaping (Error?) -> Void) {
        fetchInfo { result in
            switch result {
            case let .success(info):
                postForm(path: "sprite/\(slot)/reset",
                         fields: ["expected_rev": String(info.spriteRev)],
                         completion: completion)
            case let .failure(error):
                completion(error)
            }
        }
    }

    /// GET /sprite/{claude|codex}/raw — the animation the device is actually
    /// using, wire format [1 byte frame count][RGB565 big-endian frames...].
    static func fetchSpriteRaw(slot: String, completion: @escaping (Result<Data, Error>) -> Void) {
        guard let base = baseURL else {
            completion(.failure(Self.noHostError))
            return
        }
        var req = URLRequest(url: base.appendingPathComponent("sprite/\(slot)/raw"))
        req.timeoutInterval = 30
        URLSession.shared.dataTask(with: req) { data, resp, error in
            var result: Result<Data, Error>
            if let error = error {
                result = .failure(error)
            } else if let data = data, (resp as? HTTPURLResponse)?.statusCode == 200, data.count > 1 {
                result = .success(data)
            } else {
                result = .failure(Self.badResponseError)
            }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }

    // MARK: - internals

    private static func postForm(path: String, fields: [String: String],
                                 completion: @escaping (Error?) -> Void) {
        guard let base = baseURL else {
            completion(Self.noHostError)
            return
        }
        var req = URLRequest(url: base.appendingPathComponent(path))
        req.httpMethod = "POST"
        req.timeoutInterval = 8
        req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-._~"))
        req.httpBody = Data(fields.map { k, v in
            "\(k)=\(v.addingPercentEncoding(withAllowedCharacters: allowed) ?? v)"
        }.joined(separator: "&").utf8)
        run(req, completion: completion)
    }

    private static func run(_ req: URLRequest, completion: @escaping (Error?) -> Void) {
        URLSession.shared.dataTask(with: req) { data, resp, error in
            var err = error
            if err == nil, let http = resp as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
                let msg = data.map { String(decoding: $0, as: UTF8.self) } ?? ""
                err = NSError(domain: "DeviceClient", code: http.statusCode,
                              userInfo: [NSLocalizedDescriptionKey: "设备返回 HTTP \(http.statusCode) \(msg)"])
            }
            DispatchQueue.main.async { completion(err) }
        }.resume()
    }

    private static var noHostError: NSError {
        NSError(domain: "DeviceClient", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "未设置设备地址，请先在菜单里填写设备 IP"])
    }

    private static var badResponseError: NSError {
        NSError(domain: "DeviceClient", code: 2,
                userInfo: [NSLocalizedDescriptionKey: "设备响应解析失败"])
    }

    // MARK: - discovery / pairing

    /// Checks whether `ip` answers like our clock (GET /api/info with the
    /// expected JSON shape).
    static func verifyDevice(ip: String, timeout: TimeInterval = 2,
                             completion: @escaping (Bool) -> Void) {
        guard let url = URL(string: "http://\(ip)/api/info") else {
            completion(false)
            return
        }
        var req = URLRequest(url: url)
        req.timeoutInterval = timeout
        URLSession.shared.dataTask(with: req) { data, _, _ in
            let ok = data.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
                .map { $0["mode"] is String && $0["sprite_rev"] != nil } ?? false
            DispatchQueue.main.async { completion(ok) }
        }.resume()
    }

    private static func fetchDeviceIdentity(ip: String, completion: @escaping (String?) -> Void) {
        guard let url = URL(string: "http://\(ip)/api/info") else {
            completion(nil)
            return
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        URLSession.shared.dataTask(with: request) { data, _, _ in
            let identity = data.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
                .flatMap { object -> String? in
                    guard object["mode"] is String, object["sprite_rev"] != nil,
                          let deviceID = object["device_id"] as? String, !deviceID.isEmpty else { return nil }
                    return deviceID
                }
            DispatchQueue.main.async { completion(identity) }
        }.resume()
    }

    /// Verifies an already configured host on launch and upgrades legacy
    /// host-only preferences to an identity-bound pairing. A stale response
    /// cannot overwrite a host selected while the request was in flight.
    static func verifyConfiguredDeviceIdentity(attempts: Int = 3,
                                               completion: @escaping (Bool) -> Void) {
        let ip = configuredDeviceIP(host)
        guard !ip.isEmpty else {
            completion(false)
            return
        }
        let expectedID = UserDefaults.standard.string(forKey: deviceIDKey) ?? ""
        let generation = pairingGeneration

        func attempt(_ remaining: Int) {
            guard generation == pairingGeneration, configuredDeviceIP(host) == ip else {
                completion(false)
                return
            }
            fetchDeviceIdentity(ip: ip) { identity in
                guard generation == pairingGeneration, configuredDeviceIP(host) == ip else {
                    completion(false)
                    return
                }
                if let identity {
                    guard expectedID.isEmpty || expectedID == identity else {
                        completion(false)
                        return
                    }
                    completion(bindVerifiedDevice(ip: ip, identity: identity,
                                                  expectedGeneration: generation))
                } else if remaining > 1 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                        attempt(remaining - 1)
                    }
                } else {
                    completion(false)
                }
            }
        }
        attempt(max(1, attempts))
    }

    /// Finds the clock and pairs (sets `host`). Strategy:
    ///  1. the address that most recently polled our /status (no scanning);
    ///  2. the currently configured host, re-verified;
    ///  3. sweep this Mac's /24 subnet for /api/info (covers a factory-fresh
    ///     device that has WiFi but no bridge configured yet).
    static func autoPair(requiredDeviceID: String? = nil,
                         progress: @escaping (String) -> Void,
                         completion: @escaping (AutoPairResult) -> Void) {
        pairingGeneration &+= 1
        let generation = pairingGeneration
        var candidates: [String] = []
        if !lastSeenIP.isEmpty { candidates.append(lastSeenIP) }
        let configured = host.split(separator: ":").first.map(String.init) ?? host
        if !configured.isEmpty, !candidates.contains(configured) { candidates.append(configured) }

        func tryNext() {
            guard generation == pairingGeneration else {
                completion(.cancelled)
                return
            }
            guard let ip = candidates.first else {
                scanSubnet(requiredDeviceID: requiredDeviceID, generation: generation,
                           progress: progress, completion: completion)
                return
            }
            candidates.removeFirst()
            progress("验证 \(ip)…")
            fetchDeviceIdentity(ip: ip) { identity in
                guard generation == pairingGeneration else {
                    completion(.cancelled)
                    return
                }
                if let identity, requiredDeviceID == nil || identity == requiredDeviceID {
                    guard bindVerifiedDevice(ip: ip, identity: identity,
                                             expectedGeneration: generation) else {
                        completion(.cancelled)
                        return
                    }
                    completion(.paired(ip))
                } else {
                    tryNext()
                }
            }
        }
        tryNext()
    }

    /// Parallel sweep of the local /24 (254 hosts, ~0.8s timeout each,
    /// 32-wide). Only used when the passive route came up empty.
    private static func scanSubnet(requiredDeviceID: String?, generation: Int,
                                   progress: @escaping (String) -> Void,
                                   completion: @escaping (AutoPairResult) -> Void) {
        guard generation == pairingGeneration else {
            completion(.cancelled)
            return
        }
        guard let myIP = localIPv4(),
              let prefixEnd = myIP.range(of: ".", options: .backwards)?.lowerBound else {
            completion(.notFound)
            return
        }
        let prefix = String(myIP[..<prefixEnd])
        progress("扫描 \(prefix).1-254…")
        let session: URLSession = {
            let cfg = URLSessionConfiguration.ephemeral
            cfg.timeoutIntervalForRequest = 0.8
            cfg.httpMaximumConnectionsPerHost = 1
            return URLSession(configuration: cfg)
        }()
        let group = DispatchGroup()
        let lock = NSLock()
        var found: (ip: String, identity: String)?
        let sem = DispatchSemaphore(value: 32)
        DispatchQueue.global(qos: .utility).async {
            for n in 1...254 {
                let ip = "\(prefix).\(n)"
                if ip == myIP { continue }
                sem.wait()
                lock.lock()
                let alreadyFound = found != nil
                lock.unlock()
                if alreadyFound { sem.signal(); continue }
                group.enter()
                let task = session.dataTask(with: URL(string: "http://\(ip)/api/info")!) { data, _, _ in
                    let identity = data.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
                        .flatMap { object -> String? in
                            guard object["mode"] is String, object["sprite_rev"] != nil,
                                  let value = object["device_id"] as? String, !value.isEmpty else { return nil }
                            return value
                        }
                    if let identity, requiredDeviceID == nil || identity == requiredDeviceID {
                        lock.lock()
                        if found == nil { found = (ip, identity) }
                        lock.unlock()
                    }
                    sem.signal()
                    group.leave()
                }
                task.resume()
            }
            group.notify(queue: .main) {
                guard generation == pairingGeneration else {
                    completion(.cancelled)
                    return
                }
                if let found {
                    guard bindVerifiedDevice(ip: found.ip, identity: found.identity,
                                             expectedGeneration: generation) else {
                        completion(.cancelled)
                        return
                    }
                    completion(.paired(found.ip))
                } else {
                    completion(.notFound)
                }
            }
        }
    }

    // MARK: - pairing watchdog

    /// Stamped on every device poll of our bridge feeds (see main.swift).
    static var devicePollAt = Date.distantPast

    static func recordDevicePoll(ip: String) {
        DispatchQueue.main.async {
            let configuredIP = configuredDeviceIP(host)
            let expectedID = UserDefaults.standard.string(forKey: deviceIDKey) ?? ""
            if configuredIP == ip, !expectedID.isEmpty,
               verifiedDeviceIP == ip, verifiedDeviceID == expectedID {
                devicePollAt = Date()
                lastSeenIP = ip
                return
            }
            guard pollVerifications.insert(ip).inserted else { return }
            let generation = pairingGeneration
            fetchDeviceIdentity(ip: ip) { identity in
                pollVerifications.remove(ip)
                guard generation == pairingGeneration else { return }
                guard let identity else { return }
                let currentExpectedID = UserDefaults.standard.string(forKey: deviceIDKey) ?? ""
                let currentConfiguredIP = configuredDeviceIP(host)
                let firstPair = currentConfiguredIP.isEmpty && currentExpectedID.isEmpty
                let configuredMatch = currentConfiguredIP == ip
                    && (currentExpectedID.isEmpty || currentExpectedID == identity)
                let dhcpMatch = !currentExpectedID.isEmpty && currentExpectedID == identity
                guard firstPair || configuredMatch || dhcpMatch else { return }
                _ = bindVerifiedDevice(ip: ip, identity: identity,
                                       expectedGeneration: generation)
            }
        }
    }

    @discardableResult
    private static func bindVerifiedDevice(ip: String, identity: String,
                                           expectedGeneration: Int) -> Bool {
        guard expectedGeneration == pairingGeneration else { return false }
        pairingGeneration &+= 1
        UserDefaults.standard.set(ip, forKey: hostKey)
        UserDefaults.standard.set(identity, forKey: deviceIDKey)
        verifiedDeviceIP = ip
        verifiedDeviceID = identity
        devicePollAt = Date()
        lastSeenIP = ip
        return true
    }

    private static var healInFlight = false
    private static var lastHealAttempt = Date.distantPast

    /// Self-healing for the fresh-device chicken-and-egg: after a full flash
    /// erase the clock knows no bridge host, so it never polls us and passive
    /// discovery never fires. When we haven't heard from the device for a few
    /// minutes, actively find it (last-seen IP, configured host, then /24
    /// scan) and, if its bridge is unset or it can't reach the one it has,
    /// point it at this Mac. Called from a 60s timer; the /24 scan is
    /// rate-limited to once per 5 minutes.
    static func healPairingIfNeeded(port: UInt16) {
        guard Date().timeIntervalSince(devicePollAt) > 180 else { return } // device is polling us
        guard !healInFlight, Date().timeIntervalSince(lastHealAttempt) > 300 else { return }
        let expectedID = UserDefaults.standard.string(forKey: deviceIDKey) ?? ""
        guard !expectedID.isEmpty else { return }
        healInFlight = true
        lastHealAttempt = Date()
        autoPair(requiredDeviceID: expectedID, progress: { _ in }) { result in
            guard case .paired = result else { healInFlight = false; return }
            let generation = pairingGeneration
            fetchInfo { result in
                defer { healInFlight = false }
                guard generation == pairingGeneration else { return }
                guard case let .success(info) = result, let myIP = localIPv4() else { return }
                let stale = info.lastUpdateS < 0 || info.lastUpdateS > 60
                guard info.bridge.isEmpty || stale else { return }
                setBridgeHost("\(myIP):\(port)") { error in
                    FileHandle.standardError.write(Data(
                        "[pair] pushed bridge \(myIP):\(port) to \(info.ip): \(error.map { "\($0.localizedDescription)" } ?? "ok")\n".utf8))
                }
            }
        }
    }

    static func shouldRecordDevicePoll(path: String, ip: String,
                                       localAddresses: Set<String>) -> Bool {
        let pollPaths: Set<String> = ["/status", "/net", "/music", "/stock", "/weather"]
        return pollPaths.contains(path) && !ip.isEmpty && !ip.contains(":")
            && !ip.hasPrefix("127.") && !localAddresses.contains(ip)
    }

    static func shouldAcceptBridgePost(path: String, ip: String,
                                       deviceHost: String, lastSeenIP: String) -> Bool {
        if path == "/event" { return ip == "127.0.0.1" || ip == "::1" }
        guard path == "/stock/config", !ip.isEmpty, !ip.contains(":") else { return false }
        let configuredIP = configuredDeviceIP(deviceHost)
        if !configuredIP.isEmpty { return ip == configuredIP }
        return !lastSeenIP.isEmpty && ip == lastSeenIP
    }

    private static func configuredDeviceIP(_ raw: String) -> String {
        let host = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        let urlText = host.contains("://") ? host : "http://\(host)"
        return URLComponents(string: urlText)?.host ?? ""
    }


    private static func ipv4Interfaces() -> [(name: String, ip: String)] {
        var result: [(name: String, ip: String)] = []
        var addrs: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&addrs) == 0, let first = addrs else { return result }
        defer { freeifaddrs(addrs) }
        for ptr in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let ifa = ptr.pointee
            guard let sa = ifa.ifa_addr, sa.pointee.sa_family == UInt8(AF_INET),
                  (ifa.ifa_flags & UInt32(IFF_LOOPBACK)) == 0,
                  (ifa.ifa_flags & UInt32(IFF_UP)) != 0 else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            guard getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host, socklen_t(host.count),
                              nil, 0, NI_NUMERICHOST) == 0 else { continue }
            result.append((String(cString: ifa.ifa_name), String(cString: host)))
        }
        return result
    }

    static func localIPv4Addresses() -> Set<String> {
        Set(ipv4Interfaces().map(\.ip))
    }

    /// LAN IPv4 of this Mac (en0 preferred) — used for one-click "point the
    /// device's bridge at this Mac".
    static func localIPv4() -> String? {
        let interfaces = ipv4Interfaces()
        return interfaces.first(where: { $0.name == "en0" })?.ip ?? interfaces.first?.ip
    }
}
