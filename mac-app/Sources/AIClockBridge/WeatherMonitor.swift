import AppKit
import Foundation

/// Pulls configured-location weather from QWeather and keeps the last successful value. The
/// credential stays in Keychain; the ESP8266 only consumes this compact cache.
final class WeatherMonitor {
    struct Snapshot {
        var available = false
        var location = WeatherMonitor.locationName
        var temperatureC = 0.0
        var apparentC = 0.0
        var humidityPct = 0
        var weatherCode = -1
        var conditionZh = "天气更新中"
        var conditionEn = "Updating"
        var skycon = ""
        var windKmh = 0.0
        var highC = 0.0
        var lowC = 0.0
        var aqi = -1
        var pm25 = -1
        var airQualityZh = ""
        var sunrise = "--:--"
        var sunset = "--:--"
        var lunarZh = ""
        var updatedAt = Date(timeIntervalSince1970: 0)
        var stale = true
    }

    enum ParseError: Error {
        case malformedResponse
    }

    struct Credentials {
        let host: String
        let apiKey: String
    }

    static let textW = 232
    static let textH = 24
    static let refreshInterval: TimeInterval = 10 * 60
    static let staleAfter: TimeInterval = 30 * 60
    static let keychainService = "AIClockBridge QWeather"
    static let exampleAPIHost = "example.re.qweatherapi.com"
    static let locationName = "Beijing"
    static let displayLabel = "BEIJING"
    static let longitude = "116.4074"
    static let latitude = "39.9042"

    private let lock = NSLock()
    private let session: URLSession
    private let now: () -> Date
    private let credentialsProvider: () -> Credentials?
    private var value: Snapshot
    private var failedSinceLastSuccess = false
    private var fetchInFlight = false
    private var timer: Timer?
    private var textData = Data(count: textW * textH * 2)
    private var textRev = 0
    private var lastTextKey = ""

    init(session: URLSession? = nil, now: @escaping () -> Date = Date.init,
         credentialsProvider: @escaping () -> Credentials? = { WeatherMonitor.keychainCredentials() }) {
        self.session = session ?? Self.makeSession()
        self.now = now
        self.credentialsProvider = credentialsProvider
        var initial = Snapshot()
        initial.lunarZh = Self.lunarText(for: now())
        value = initial
        if Thread.isMainThread, let rendered = Self.renderTextStrip(initial) {
            textData = rendered
            textRev = 1
            lastTextKey = Self.textKey(initial)
        }
    }

    func start() {
        scheduleTextRender(snapshot)
        fetch()
        timer = Timer.scheduledTimer(withTimeInterval: Self.refreshInterval, repeats: true) {
            [weak self] _ in self?.fetch()
        }
    }

    var snapshot: Snapshot {
        let currentDate = now()
        let lunar = Self.lunarText(for: currentDate)
        lock.lock()
        let lunarChanged = value.lunarZh != lunar
        if lunarChanged { value.lunarZh = lunar }
        var current = value
        let failed = failedSinceLastSuccess
        lock.unlock()
        current.stale = !current.available || failed
            || currentDate.timeIntervalSince(current.updatedAt) > Self.staleAfter
        if lunarChanged { scheduleTextRender(current) }
        return current
    }

    func jsonData() -> Data {
        let current = snapshot
        lock.lock()
        let revision = textRev
        lock.unlock()
        let object: [String: Any] = [
            "available": current.available,
            "location": current.location,
            "temperature_c": current.available ? current.temperatureC : NSNull(),
            "apparent_c": current.available ? current.apparentC : NSNull(),
            "humidity_pct": current.available ? current.humidityPct : NSNull(),
            "weather_code": current.available ? current.weatherCode : NSNull(),
            "condition_zh": current.conditionZh,
            "condition_en": current.conditionEn,
            "skycon": current.skycon,
            "aqi": current.available ? current.aqi : NSNull(),
            "pm25": current.available ? current.pm25 : NSNull(),
            "air_quality_zh": current.airQualityZh,
            "provider": "qweather",
            "wind_kmh": current.available ? current.windKmh : NSNull(),
            "high_c": current.available ? current.highC : NSNull(),
            "low_c": current.available ? current.lowC : NSNull(),
            "sunrise": current.sunrise,
            "sunset": current.sunset,
            "lunar_zh": current.lunarZh,
            "updated_at": current.available ? Int(current.updatedAt.timeIntervalSince1970) : 0,
            "stale": current.stale,
            "text_rev": revision,
        ]
        return (try? JSONSerialization.data(withJSONObject: object)) ?? Data("{}".utf8)
    }

    /// Fixed 232x24 RGB565 big-endian strip, with no length/header prefix.
    func textRGB565() -> Data {
        lock.lock()
        defer { lock.unlock() }
        return textData
    }

    private func fetch() {
        lock.lock()
        guard !fetchInFlight else {
            lock.unlock()
            return
        }
        fetchInFlight = true
        lock.unlock()

        guard let rawCredentials = credentialsProvider() else {
            applyFailure()
            return
        }
        let credentials = Credentials(
            host: rawCredentials.host.trimmingCharacters(in: .whitespacesAndNewlines).lowercased(),
            apiKey: rawCredentials.apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        )
        guard Self.validAPIHost(credentials.host), Self.validToken(credentials.apiKey),
              let nowURL = Self.apiURL(host: credentials.host, path: "/v7/weather/now", queryItems: [
                URLQueryItem(name: "location", value: "\(Self.longitude),\(Self.latitude)"),
                URLQueryItem(name: "lang", value: "zh"),
                URLQueryItem(name: "unit", value: "m"),
              ]),
              let dailyURL = Self.apiURL(host: credentials.host, path: "/v7/weather/3d", queryItems: [
                URLQueryItem(name: "location", value: "\(Self.longitude),\(Self.latitude)"),
                URLQueryItem(name: "lang", value: "zh"),
                URLQueryItem(name: "unit", value: "m"),
              ]),
              let airURL = Self.apiURL(host: credentials.host,
                                       path: "/airquality/v1/current/\(Self.latitude)/\(Self.longitude)",
                                       queryItems: [URLQueryItem(name: "lang", value: "zh")]) else {
            applyFailure()
            return
        }

        Task { [weak self] in
            guard let self else { return }
            do {
                async let nowData = self.fetchData(url: nowURL, apiKey: credentials.apiKey)
                async let dailyData = self.fetchData(url: dailyURL, apiKey: credentials.apiKey)
                async let airData = self.fetchData(url: airURL, apiKey: credentials.apiKey)
                let payloads = try await (nowData, dailyData, airData)
                let parsed = try Self.parse(nowData: payloads.0, dailyData: payloads.1,
                                            airData: payloads.2, now: self.now())
                self.applySuccess(parsed)
            } catch {
                self.applyFailure()
            }
        }
    }

    private func fetchData(url: URL, apiKey: String) async throws -> Data {
        var request = URLRequest(url: url)
        request.timeoutInterval = 10
        request.setValue(apiKey, forHTTPHeaderField: "X-QW-Api-Key")
        let (data, response) = try await session.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard (200...299).contains(status), !data.isEmpty else { throw ParseError.malformedResponse }
        return data
    }

    private func applySuccess(_ next: Snapshot) {
        lock.lock()
        value = next
        failedSinceLastSuccess = false
        fetchInFlight = false
        lock.unlock()
        scheduleTextRender(next)
    }

    private func applyFailure() {
        lock.lock()
        failedSinceLastSuccess = true
        fetchInFlight = false
        var current = value
        lock.unlock()
        current.stale = true
        scheduleTextRender(current)
    }

    static func parse(nowData: Data, dailyData: Data, airData: Data, now: Date) throws -> Snapshot {
        guard let nowRoot = try? JSONSerialization.jsonObject(with: nowData) as? [String: Any],
              nowRoot["code"] as? String == "200",
              let current = nowRoot["now"] as? [String: Any],
              let observationText = current["obsTime"] as? String,
              let observationTime = iso8601Date(observationText),
              let temperature = double(current["temp"]),
              let apparent = double(current["feelsLike"]),
              let humidity = int(current["humidity"]),
              let wind = double(current["windSpeed"]),
              let conditionZh = current["text"] as? String,
              let dailyRoot = try? JSONSerialization.jsonObject(with: dailyData) as? [String: Any],
              dailyRoot["code"] as? String == "200",
              let dailyRows = dailyRoot["daily"] as? [[String: Any]],
              let today = dailyRows.first,
              let high = double(today["tempMax"]),
              let low = double(today["tempMin"]),
              let airRoot = try? JSONSerialization.jsonObject(with: airData) as? [String: Any],
              let indexes = airRoot["indexes"] as? [[String: Any]],
              let airIndex = indexes.first(where: { ($0["code"] as? String) == "cn-mee" })
                ?? indexes.first(where: { ($0["code"] as? String) == "cn-mee-1h" }),
              let aqi = int(airIndex["aqi"]),
              let airQualityZh = airIndex["category"] as? String else {
            throw ParseError.malformedResponse
        }
        let age = now.timeIntervalSince(observationTime)
        guard (-80...60).contains(temperature), (-100...100).contains(apparent),
              (0...100).contains(humidity), (0...500).contains(wind),
              low <= high, (-80...60).contains(low), (-80...60).contains(high),
              (0...500).contains(aqi), age <= staleAfter, age >= -300 else {
            throw ParseError.malformedResponse
        }
        let pollutants = airRoot["pollutants"] as? [[String: Any]]
        let pm25Object = pollutants?.first(where: { ($0["code"] as? String) == "pm2p5" })
        let pm25Concentration = pm25Object?["concentration"] as? [String: Any]
        let pm25 = int(pm25Concentration?["value"]) ?? -1
        guard pm25 >= -1 else { throw ParseError.malformedResponse }
        let skycon = qweatherSkycon(icon: current["icon"] as? String ?? "", text: conditionZh)
        let condition = condition(for: skycon)
        return Snapshot(
            available: true,
            location: locationName,
            temperatureC: temperature,
            apparentC: apparent,
            humidityPct: humidity,
            weatherCode: weatherCode(for: skycon),
            conditionZh: conditionZh,
            conditionEn: condition.en,
            skycon: skycon,
            windKmh: wind,
            highC: high,
            lowC: low,
            aqi: aqi,
            pm25: pm25,
            airQualityZh: airQualityZh,
            sunrise: today["sunrise"] as? String ?? "--:--",
            sunset: today["sunset"] as? String ?? "--:--",
            lunarZh: lunarText(for: now),
            updatedAt: observationTime,
            stale: false
        )
    }

    static func lunarText(for date: Date) -> String {
        var calendar = Calendar(identifier: .chinese)
        calendar.timeZone = TimeZone(identifier: "Asia/Shanghai") ?? .current
        let components = calendar.dateComponents([.month, .day], from: date)
        guard let month = components.month, let day = components.day,
              (1...12).contains(month), (1...30).contains(day) else {
            return "农历"
        }
        let months = ["正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"]
        let leap = components.isLeapMonth == true ? "闰" : ""
        return "农历\(leap)\(months[month - 1])月\(lunarDay(day))"
    }

    static func condition(for skycon: String) -> (zh: String, en: String) {
        switch skycon {
        case "CLEAR_DAY", "CLEAR_NIGHT": return ("晴", "Clear")
        case "PARTLY_CLOUDY_DAY", "PARTLY_CLOUDY_NIGHT": return ("多云", "Partly cloudy")
        case "CLOUDY": return ("阴", "Cloudy")
        case "LIGHT_RAIN": return ("小雨", "Light rain")
        case "MODERATE_RAIN": return ("中雨", "Rain")
        case "HEAVY_RAIN": return ("大雨", "Heavy rain")
        case "STORM_RAIN": return ("暴雨", "Storm")
        case "LIGHT_SNOW": return ("小雪", "Light snow")
        case "MODERATE_SNOW": return ("中雪", "Snow")
        case "HEAVY_SNOW", "STORM_SNOW": return ("大雪", "Heavy snow")
        case "FOG": return ("雾", "Fog")
        case "LIGHT_HAZE", "MODERATE_HAZE", "HEAVY_HAZE": return ("霾", "Haze")
        case "DUST": return ("浮尘", "Dust")
        case "SAND": return ("沙尘", "Sand")
        case "WIND": return ("大风", "Windy")
        default: return ("天气", "Weather")
        }
    }

    static func weatherCode(for skycon: String) -> Int {
        switch skycon {
        case "CLEAR_DAY", "CLEAR_NIGHT": return 0
        case "PARTLY_CLOUDY_DAY", "PARTLY_CLOUDY_NIGHT": return 2
        case "CLOUDY": return 3
        case "FOG": return 45
        case "LIGHT_HAZE", "MODERATE_HAZE", "HEAVY_HAZE": return 48
        case "DUST", "SAND": return 48
        case "LIGHT_RAIN": return 61
        case "MODERATE_RAIN": return 63
        case "HEAVY_RAIN": return 65
        case "STORM_RAIN": return 82
        case "LIGHT_SNOW": return 71
        case "MODERATE_SNOW": return 73
        case "HEAVY_SNOW": return 75
        case "STORM_SNOW": return 86
        default: return 1
        }
    }

    static func qweatherSkycon(icon: String, text: String) -> String {
        switch icon {
        case "100": return "CLEAR_DAY"
        case "150": return "CLEAR_NIGHT"
        case "101", "102", "103": return "PARTLY_CLOUDY_DAY"
        case "151", "152", "153": return "PARTLY_CLOUDY_NIGHT"
        case "104": return "CLOUDY"
        case "300", "305", "309", "314", "350", "399": return "LIGHT_RAIN"
        case "306", "313", "315": return "MODERATE_RAIN"
        case "301", "307", "308", "316", "317", "351": return "HEAVY_RAIN"
        case "302", "303", "304", "310", "311", "312", "318": return "STORM_RAIN"
        case "400", "404", "405", "406", "407", "408", "456", "457", "499": return "LIGHT_SNOW"
        case "401", "409": return "MODERATE_SNOW"
        case "402", "403", "410": return "HEAVY_SNOW"
        case "500", "501", "509", "510", "514", "515": return "FOG"
        case "502", "511", "512", "513": return "MODERATE_HAZE"
        case "503", "507", "508": return "SAND"
        case "504": return "DUST"
        default: break
        }
        if text.contains("暴雨") || text.contains("特大") { return "STORM_RAIN" }
        if text.contains("雷阵雨") || text.contains("冰雹") { return "STORM_RAIN" }
        if text.contains("强阵雨") || text.contains("大雨") { return "HEAVY_RAIN" }
        if text.contains("中雨") || text.contains("冻雨") { return "MODERATE_RAIN" }
        if text.contains("暴雪") { return "STORM_SNOW" }
        if text.contains("大雪") { return "HEAVY_SNOW" }
        if text.contains("中雪") { return "MODERATE_SNOW" }
        if text.contains("雨夹雪") || text.contains("阵雪") || text.contains("雪") { return "LIGHT_SNOW" }
        if text.contains("雨") { return "LIGHT_RAIN" }
        if text.contains("霾") { return "MODERATE_HAZE" }
        if text.contains("雾") { return "FOG" }
        if text.contains("沙") { return "SAND" }
        if text.contains("尘") { return "DUST" }
        if text.contains("风") { return "WIND" }
        return "WEATHER"
    }

    private func scheduleTextRender(_ current: Snapshot) {
        let key = Self.textKey(current)
        lock.lock()
        let dirty = key != lastTextKey
        lock.unlock()
        guard dirty else { return }
        let render = { [weak self] in
            guard let self, let data = Self.renderTextStrip(current) else { return }
            self.lock.lock()
            if key != self.lastTextKey {
                self.lastTextKey = key
                self.textData = data
                self.textRev += 1
            }
            self.lock.unlock()
        }
        if Thread.isMainThread {
            render()
        } else {
            DispatchQueue.main.async(execute: render)
        }
    }

    private static func textKey(_ current: Snapshot) -> String {
        current.lunarZh
    }

    private static func renderTextStrip(_ current: Snapshot) -> Data? {
        precondition(Thread.isMainThread)
        let w = textW, h = textH
        guard let context = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8,
                                      bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return nil
        }
        context.setFillColor(CGColor(red: 0, green: 0, blue: 0, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: w, height: h))

        let text = NSMutableAttributedString(string: current.lunarZh, attributes: [
            .font: NSFont.systemFont(ofSize: 15, weight: .semibold),
            .foregroundColor: NSColor.white,
        ])
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        style.lineBreakMode = .byClipping
        text.addAttribute(.paragraphStyle, value: style, range: NSRange(location: 0, length: text.length))

        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: false)
        text.draw(in: NSRect(x: 1, y: 0, width: w - 2, height: h))
        NSGraphicsContext.restoreGraphicsState()

        guard let rendered = context.data else { return nil }
        let pixels = rendered.bindMemory(to: UInt8.self, capacity: w * h * 4)
        var output = Data(capacity: w * h * 2)
        for index in 0..<(w * h) {
            let red = pixels[index * 4]
            let green = pixels[index * 4 + 1]
            let blue = pixels[index * 4 + 2]
            let rgb565 = (UInt16(red & 0xF8) << 8) | (UInt16(green & 0xFC) << 3)
                | UInt16(blue >> 3)
            output.append(UInt8(rgb565 >> 8))
            output.append(UInt8(rgb565 & 0xFF))
        }
        return output
    }

    static func validToken(_ token: String) -> Bool {
        let bytes = Array(token.utf8)
        guard (8...64).contains(bytes.count) else { return false }
        return bytes.allSatisfy { byte in
            (48...57).contains(byte) || (65...90).contains(byte) || (97...122).contains(byte)
                || byte == 45 || byte == 95
        }
    }

    static func validAPIHost(_ host: String) -> Bool {
        let bytes = Array(host.utf8)
        guard !bytes.isEmpty, bytes.count <= 95, host == host.lowercased(),
              host.hasSuffix(".re.qweatherapi.com"), !host.contains("..") else { return false }
        let labels = host.split(separator: ".", omittingEmptySubsequences: false)
        return labels.allSatisfy { label in
            guard !label.isEmpty, label.utf8.count <= 63,
                  label.first != "-", label.last != "-" else { return false }
            return label.utf8.allSatisfy { byte in
                (48...57).contains(byte) || (97...122).contains(byte) || byte == 45
            }
        }
    }

    static func validSkycon(_ value: String) -> Bool {
        let bytes = Array(value.utf8)
        guard !bytes.isEmpty, bytes.count <= 31 else { return false }
        return bytes.allSatisfy { $0 == 95 || (65...90).contains($0) }
    }

    static func keychainCredentials() -> Credentials? {
        guard let apiKey = keychainValue(account: "api-key"),
              let host = keychainValue(account: "api-host"),
              validToken(apiKey), validAPIHost(host) else { return nil }
        return Credentials(host: host, apiKey: apiKey)
    }

    private static func keychainValue(account: String) -> String? {
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/security")
        process.arguments = ["find-generic-password", "-a", account, "-s", keychainService, "-w"]
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            return nil
        }
        guard process.terminationStatus == 0 else { return nil }
        let value = String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return value.isEmpty ? nil : value
    }

    private static func makeSession() -> URLSession {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.urlCache = nil
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: configuration)
    }

    static func apiURL(host: String, path: String, queryItems: [URLQueryItem]) -> URL? {
        guard validAPIHost(host), path.hasPrefix("/") else { return nil }
        var components = URLComponents()
        components.scheme = "https"
        components.host = host
        components.path = path
        components.queryItems = queryItems
        return components.url
    }

    private static func lunarDay(_ day: Int) -> String {
        let digits = ["一", "二", "三", "四", "五", "六", "七", "八", "九", "十"]
        switch day {
        case 1...10: return day == 10 ? "初十" : "初\(digits[day - 1])"
        case 11...19: return "十\(digits[day - 11])"
        case 20: return "二十"
        case 21...29: return "廿\(digits[day - 21])"
        case 30: return "三十"
        default: return ""
        }
    }

    private static func double(_ value: Any?) -> Double? {
        if let number = value as? NSNumber { return number.doubleValue }
        if let string = value as? String { return Double(string) }
        return nil
    }

    private static func int(_ value: Any?) -> Int? {
        if let number = value as? NSNumber { return number.intValue }
        if let string = value as? String, let number = Double(string), number.isFinite {
            return Int(number.rounded())
        }
        return nil
    }

    private static func iso8601Date(_ value: String) -> Date? {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        if let date = formatter.date(from: value) { return date }
        let minuteFormatter = DateFormatter()
        minuteFormatter.locale = Locale(identifier: "en_US_POSIX")
        minuteFormatter.dateFormat = "yyyy-MM-dd'T'HH:mmXXXXX"
        return minuteFormatter.date(from: value)
    }

}
