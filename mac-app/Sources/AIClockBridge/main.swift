import AppKit

// Entry point. Runs as an "accessory" app (menu-bar only, no Dock icon, no main
// window) and starts the /status HTTP server that the ESP8266 clock polls.
if CommandLine.arguments.count >= 3, CommandLine.arguments[1] == "--render-weather-themes" {
    let outputDirectory = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
    try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true)
    var sample = WeatherMonitor.Snapshot()
    sample.available = true
    sample.temperatureC = 25.6
    sample.apparentC = 27.8
    sample.humidityPct = 91
    sample.weatherCode = 2
    sample.conditionZh = "多云"
    sample.conditionEn = "Partly cloudy"
    sample.skycon = "PARTLY_CLOUDY_DAY"
    sample.windKmh = 12
    sample.highC = 28.7
    sample.lowC = 25.4
    sample.aqi = 18
    sample.pm25 = 6
    sample.airQualityZh = "优"
    sample.lunarZh = "农历六月初二"
    sample.updatedAt = Date()
    sample.stale = false
    var edge = sample
    edge.apparentC = -100
    edge.windKmh = 500
    edge.highC = 60
    edge.lowC = -80
    edge.humidityPct = 100
    edge.aqi = 500
    edge.skycon = ""
    edge.lunarZh = "农历闰十一月廿九"
    func render(_ value: WeatherMonitor.Snapshot, prefix: String) throws {
        for theme in ["classic", "minimal", "dashboard"] {
            let view = MirrorView(frame: NSRect(x: 0, y: 0, width: 240, height: 240))
            view.weatherMode = true
            view.weatherTheme = theme
            view.weather = value
            guard let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
            view.cacheDisplay(in: view.bounds, to: bitmap)
            guard let png = bitmap.representation(using: .png, properties: [:]) else { exit(1) }
            try png.write(to: outputDirectory.appendingPathComponent("\(prefix)-\(theme).png"))
        }
    }
    try render(sample, prefix: "weather")
    try render(edge, prefix: "weather-edge")
    let iconCases = [
        ("clear-day", "CLEAR_DAY"), ("clear-night", "CLEAR_NIGHT"),
        ("partly-day", "PARTLY_CLOUDY_DAY"), ("partly-night", "PARTLY_CLOUDY_NIGHT"),
        ("cloudy", "CLOUDY"), ("light-rain", "LIGHT_RAIN"),
        ("moderate-rain", "MODERATE_RAIN"), ("heavy-rain", "HEAVY_RAIN"),
        ("storm-rain", "STORM_RAIN"), ("light-snow", "LIGHT_SNOW"),
        ("moderate-snow", "MODERATE_SNOW"), ("heavy-snow", "HEAVY_SNOW"),
        ("fog", "FOG"), ("haze", "MODERATE_HAZE"), ("dust", "DUST"),
        ("wind", "WIND"), ("unknown", ""),
    ]
    for (name, skycon) in iconCases {
        var iconSample = sample
        iconSample.skycon = skycon
        try render(iconSample, prefix: "weather-icon-\(name)")
    }
    print("weather theme renders: ok")
    exit(0)
}

if CommandLine.arguments.count >= 2, CommandLine.arguments[1] == "--self-test-bridge" {
    var failures: [String] = []
    func check(_ condition: @autoclosure () -> Bool, _ label: String) {
        if !condition() { failures.append(label) }
    }

    check(marketColorRole(up: 1) == .gain, "gain color role")
    check(marketColorRole(up: -1) == .loss, "loss color role")
    check(marketColorRole(up: 0) == .neutral, "neutral color role")
    let weatherIconCount = WeatherIconKind.unknown.rawValue + 1
    check(WeatherIconData.bytes.count == weatherIconCount * WeatherIconData.bytesPerIcon,
          "weather icon bitmap size")
    let weatherIconPayloads = (0..<weatherIconCount).map { index -> Data in
        let start = index * WeatherIconData.bytesPerIcon
        return Data(WeatherIconData.bytes[start..<(start + WeatherIconData.bytesPerIcon)])
    }
    check(weatherIconPayloads.allSatisfy { $0.contains(where: { $0 != 0 }) },
          "weather icon bitmap nonempty")
    check(Set(weatherIconPayloads).count == weatherIconCount, "weather icon bitmaps distinct")
    check(WeatherMonitor.qweatherSkycon(icon: "301", text: "强阵雨") == "HEAVY_RAIN",
          "QWeather heavy shower icon mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "304", text: "雷阵雨伴有冰雹") == "STORM_RAIN",
          "QWeather hail icon mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "404", text: "雨夹雪") == "LIGHT_SNOW",
          "QWeather sleet icon mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "314", text: "小到中雨") == "LIGHT_RAIN",
          "QWeather rain transition lower-bound mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "315", text: "中到大雨") == "MODERATE_RAIN",
          "QWeather rain transition midpoint mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "316", text: "大雨到暴雨") == "HEAVY_RAIN",
          "QWeather rain transition upper-bound mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "408", text: "小到中雪") == "LIGHT_SNOW",
          "QWeather snow transition lower-bound mapping")
    check(WeatherMonitor.qweatherSkycon(icon: "409", text: "中到大雪") == "MODERATE_SNOW",
          "QWeather snow transition upper-bound mapping")
    check(!DeviceClient.shouldRecordDevicePoll(path: "/weather", ip: "192.0.2.20",
                                               localAddresses: ["192.0.2.20"]),
          "local bridge request excluded from discovery")
    check(DeviceClient.shouldRecordDevicePoll(path: "/weather", ip: "192.0.2.10",
                                              localAddresses: ["192.0.2.20"]),
          "device poll accepted for discovery")
    check(!DeviceClient.shouldRecordDevicePoll(path: "/stock/config", ip: "192.0.2.10",
                                               localAddresses: ["192.0.2.20"]),
          "non-poll route excluded from discovery")
    check(!DeviceClient.shouldRecordDevicePoll(path: "/weather", ip: "fe80::1",
                                               localAddresses: []),
          "IPv6 source excluded from ESP8266 discovery")
    let validLengthLines = "POST / HTTP/1.1\r\nContent-Length: 42"
        .split(whereSeparator: { $0.isNewline })
    let negativeLengthLines = "POST / HTTP/1.1\r\nContent-Length: -1"
        .split(whereSeparator: { $0.isNewline })
    let hugeLengthLines = "POST / HTTP/1.1\r\nContent-Length: 9223372036854775807"
        .split(whereSeparator: { $0.isNewline })
    let conflictingLengthLines = "Content-Length: 1\r\nContent-Length: 2"
        .split(whereSeparator: { $0.isNewline })
    check(HTTPServer.contentLength(in: validLengthLines) == 42, "valid HTTP content length")
    check(HTTPServer.contentLength(in: negativeLengthLines) == nil, "negative HTTP content length")
    check(HTTPServer.contentLength(in: hugeLengthLines) == nil, "oversize HTTP content length")
    check(HTTPServer.contentLength(in: conflictingLengthLines) == nil,
          "conflicting HTTP content length")
    check(DeviceClient.shouldAcceptBridgePost(path: "/event", ip: "127.0.0.1",
                                              deviceHost: "192.0.2.10", lastSeenIP: ""),
          "local hook POST accepted")
    check(!DeviceClient.shouldAcceptBridgePost(path: "/event", ip: "192.0.2.90",
                                               deviceHost: "192.0.2.10", lastSeenIP: ""),
          "LAN hook POST rejected")
    check(DeviceClient.shouldAcceptBridgePost(path: "/stock/config", ip: "192.0.2.10",
                                              deviceHost: "http://192.0.2.10:80", lastSeenIP: ""),
          "configured device POST accepted")
    check(DeviceClient.shouldAcceptBridgePost(path: "/stock/config", ip: "192.0.2.10",
                                              deviceHost: "", lastSeenIP: "192.0.2.10"),
          "last-seen device POST accepted")
    check(!DeviceClient.shouldAcceptBridgePost(path: "/stock/config", ip: "192.0.2.90",
                                               deviceHost: "192.0.2.10", lastSeenIP: "192.0.2.10"),
          "unknown device POST rejected")
    check(!DeviceClient.shouldAcceptBridgePost(path: "/stock/config", ip: "192.0.2.90",
                                               deviceHost: "192.0.2.10", lastSeenIP: "192.0.2.90"),
          "configured device overrides poisoned last-seen address")

    let quotaNow = 1_000_000.0
    let legacyQuota = UsageFetcher.parsedCodexUsage(rateLimit: [
        "primary_window": ["used_percent": 12, "limit_window_seconds": 18_000,
                           "reset_at": quotaNow + 3_600],
        "secondary_window": ["used_percent": 34, "limit_window_seconds": 604_800,
                             "reset_at": quotaNow + 7_200],
    ], now: quotaNow)
    check(legacyQuota.primaryPct == 12 && legacyQuota.weeklyPct == 34,
          "legacy Codex quota windows")
    let weeklyOnlyQuota = UsageFetcher.parsedCodexUsage(rateLimit: [
        "primary_window": ["used_percent": 43, "limit_window_seconds": 604_800,
                           "reset_at": quotaNow + 3_600],
        "secondary_window": NSNull(),
    ], now: quotaNow)
    check(weeklyOnlyQuota.primaryPct == nil && weeklyOnlyQuota.weeklyPct == 43
          && weeklyOnlyQuota.weeklyResetMin == 60, "weekly-only Codex API window")
    let fallbackQuota = UsageFetcher.parsedCodexUsage(rateLimit: [
        "primary_window": ["used_percent": 15],
        "secondary_window": ["used_percent": 35],
    ], now: quotaNow)
    check(fallbackQuota.primaryPct == 15 && fallbackQuota.weeklyPct == 35,
          "Codex slot fallback without window length")
    let logQuota = StatusService.parsedCodexQuota(rateLimits: [
        "primary": ["used_percent": 44, "window_minutes": 10_080,
                    "resets_at": quotaNow + 7_200],
    ], now: quotaNow)
    check(logQuota.primaryPct == nil && logQuota.weeklyPct == 44
          && logQuota.weeklyWindowMin == 10_080, "weekly-only Codex log window")
    let weeklyDisplay = codexQuotaDisplay(primaryPct: nil, weeklyPct: 43)
    check(weeklyDisplay.weeklyOnly && weeklyDisplay.ringPct == 43,
          "weekly-only Codex mirror")
    let legacyDisplay = codexQuotaDisplay(primaryPct: 12, weeklyPct: 34)
    check(!legacyDisplay.weeklyOnly && legacyDisplay.ringPct == 12,
          "legacy Codex mirror")
    do {
        let statusRoot = FileManager.default.temporaryDirectory
            .appendingPathComponent("AIClockBridge-status-self-test-\(UUID().uuidString)",
                                    isDirectory: true)
        defer { try? FileManager.default.removeItem(at: statusRoot) }
        let claudeRoot = statusRoot.appendingPathComponent("claude", isDirectory: true)
        let codexRoot = statusRoot.appendingPathComponent("codex", isDirectory: true)
        try FileManager.default.createDirectory(at: claudeRoot, withIntermediateDirectories: true)
        let statusDate = ISO8601DateFormatter().date(from: "2026-07-15T06:30:00Z")!
        var statusNow = statusDate.timeIntervalSince1970
        var statusUptime = 1_000.0
        func codexDayDirectory(_ epoch: TimeInterval) -> URL {
            let components = Calendar.current.dateComponents([.year, .month, .day],
                                                             from: Date(timeIntervalSince1970: epoch))
            return codexRoot
                .appendingPathComponent(String(format: "%04d", components.year!), isDirectory: true)
                .appendingPathComponent(String(format: "%02d", components.month!), isDirectory: true)
                .appendingPathComponent(String(format: "%02d", components.day!), isDirectory: true)
        }
        func append(_ data: Data, to url: URL) throws {
            let handle = try FileHandle(forWritingTo: url)
            defer { try? handle.close() }
            try handle.seekToEnd()
            try handle.write(contentsOf: data)
        }

        let codexDay = codexDayDirectory(statusNow)
        try FileManager.default.createDirectory(at: codexDay, withIntermediateDirectories: true)
        let claudeURL = claudeRoot.appendingPathComponent("session.jsonl")
        let claudeLine = Data(#"{"timestamp":"2026-07-15T06:00:00Z","message":{"usage":{"input_tokens":10,"output_tokens":5,"cache_creation_input_tokens":2,"cache_read_input_tokens":3}}}"#.utf8)
        var claudeData = claudeLine
        claudeData.append(0x0a)
        try claudeData.write(to: claudeURL)

        let codexURL = codexDay.appendingPathComponent("rollout.jsonl")
        let initialCodexLine = Data(#"{"timestamp":"2026-07-15T06:05:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":1234}},"rate_limits":{"primary":{"used_percent":12,"window_minutes":300,"resets_at":1784098800},"secondary":{"used_percent":34,"window_minutes":10080,"resets_at":1784102400}}}}"#.utf8)
        var codexData = Data(repeating: 0x78, count: 4_600_000)
        codexData.append(0x0a)
        codexData.append(initialCodexLine)
        codexData.append(0x0a)
        try codexData.write(to: codexURL)

        let statusService = StatusService(claudeDir: claudeRoot, codexDir: codexRoot,
                                          now: { statusNow }, uptime: { statusUptime },
                                          refreshInterval: 3_600)
        statusService.refreshSynchronouslyForTesting()
        var statusSnapshot = statusService.snapshot()
        var statusMetrics = statusService.debugMetrics
        check(statusSnapshot.claude.tokensToday == 20, "incremental Claude bootstrap")
        check(statusSnapshot.codex.tokensToday == 1234
              && statusSnapshot.codex.primaryPct == 12
              && statusSnapshot.codex.weeklyPct == 34, "reverse Codex bootstrap")
        check(statusMetrics.claudeBytesRead == claudeData.count
              && statusMetrics.codexBytesRead <= 512 * 1024,
              "bounded initial Codex tail read")
        let snapshotStart = CFAbsoluteTimeGetCurrent()
        for _ in 0..<100 { _ = statusService.snapshot() }
        check(CFAbsoluteTimeGetCurrent() - snapshotStart < 0.1
              && statusService.debugMetrics.scanCount == 1, "nonblocking in-memory snapshot")

        let partialCodexLine = Data(#"{"timestamp":"2026-07-15T06:20:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":2500}}}}"#.utf8)
        try append(partialCodexLine, to: codexURL)
        statusUptime += 15
        statusService.refreshSynchronouslyForTesting()
        statusSnapshot = statusService.snapshot()
        statusMetrics = statusService.debugMetrics
        check(statusSnapshot.codex.tokensToday == 1234
              && statusMetrics.codexBytesRead == partialCodexLine.count,
              "partial JSONL line held until newline")
        try append(Data([0x0a]), to: codexURL)
        statusUptime += 15
        statusService.refreshSynchronouslyForTesting()
        statusSnapshot = statusService.snapshot()
        check(statusSnapshot.codex.tokensToday == 2500
              && statusService.debugMetrics.codexBytesRead == 1,
              "incremental JSONL append")

        var replacement = Data(#"{"timestamp":"2026-07-15T06:25:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":333}},"rate_limits":{"primary":{"used_percent":21,"window_minutes":300}}}}"#.utf8)
        replacement.append(0x0a)
        try replacement.write(to: codexURL, options: .atomic)
        statusUptime += 15
        statusService.refreshSynchronouslyForTesting()
        statusSnapshot = statusService.snapshot()
        check(statusSnapshot.codex.tokensToday == 333
              && statusSnapshot.codex.primaryPct == 21, "JSONL replacement rebuild")

        statusNow += 24 * 60 * 60
        statusUptime += 15
        let nextDay = codexDayDirectory(statusNow)
        try FileManager.default.createDirectory(at: nextDay, withIntermediateDirectories: true)
        var nextDayLine = Data(#"{"timestamp":"2026-07-16T06:00:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":77}}}}"#.utf8)
        nextDayLine.append(0x0a)
        try nextDayLine.write(to: nextDay.appendingPathComponent("rollout.jsonl"))
        statusService.refreshSynchronouslyForTesting()
        check(statusService.snapshot().codex.tokensToday == 77, "JSONL day rollover")

        let resilientClaudeRoot = statusRoot.appendingPathComponent("resilient-claude", isDirectory: true)
        let resilientCodexRoot = statusRoot.appendingPathComponent("resilient-codex", isDirectory: true)
        let resilientNow = statusDate.timeIntervalSince1970
        var resilientUptime = 2_000.0
        try FileManager.default.createDirectory(at: resilientClaudeRoot, withIntermediateDirectories: true)
        let resilientClaudeURL = resilientClaudeRoot.appendingPathComponent("session.jsonl")
        try claudeData.write(to: resilientClaudeURL)
        var blockedOpenPath: String?
        var failingReadCall: Int?
        var readCall = 0
        let resilientService = StatusService(
            claudeDir: resilientClaudeRoot,
            codexDir: resilientCodexRoot,
            now: { resilientNow },
            uptime: { resilientUptime },
            refreshInterval: 3_600,
            openFile: { url in
                if url.resolvingSymlinksInPath().path == blockedOpenPath {
                    throw NSError(domain: "AIClockBridgeStatusSelfTest", code: 1)
                }
                return try FileHandle(forReadingFrom: url)
            },
            readChunk: { handle, count in
                readCall += 1
                if readCall == failingReadCall {
                    throw NSError(domain: "AIClockBridgeStatusSelfTest", code: 2)
                }
                return try handle.read(upToCount: count)
            }
        )
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().claude.tokensToday == 20,
              "IO recovery baseline")

        let secondClaudeLine = Data(#"{"timestamp":"2026-07-15T06:10:00Z","message":{"usage":{"input_tokens":4,"output_tokens":3}}}"#.utf8)
        var multiChunkAppend = secondClaudeLine
        multiChunkAppend.append(0x0a)
        multiChunkAppend.append(Data(repeating: 0x78, count: 300_000))
        multiChunkAppend.append(0x0a)
        try append(multiChunkAppend, to: resilientClaudeURL)
        readCall = 0
        failingReadCall = 2
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().claude.tokensToday == 20,
              "mid-read failure preserves last-good")
        readCall = 0
        failingReadCall = nil
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().claude.tokensToday == 27,
              "mid-read failure retries without duplicate")

        try FileManager.default.removeItem(at: resilientClaudeRoot)
        try Data("not a directory".utf8).write(to: resilientClaudeRoot)
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().claude.tokensToday == 27,
              "directory scan failure preserves last-good")
        try FileManager.default.removeItem(at: resilientClaudeRoot)
        try FileManager.default.createDirectory(at: resilientClaudeRoot, withIntermediateDirectories: true)
        var recoveredClaude = Data(#"{"timestamp":"2026-07-15T06:20:00Z","message":{"usage":{"input_tokens":2,"output_tokens":1}}}"#.utf8)
        recoveredClaude.append(0x0a)
        try recoveredClaude.write(to: resilientClaudeURL)
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().claude.tokensToday == 3,
              "directory scan recovery rebuilds index")

        let resilientComponents = Calendar.current.dateComponents(
            [.year, .month, .day], from: Date(timeIntervalSince1970: resilientNow)
        )
        let resilientCodexDay = resilientCodexRoot
            .appendingPathComponent(String(format: "%04d", resilientComponents.year!), isDirectory: true)
            .appendingPathComponent(String(format: "%02d", resilientComponents.month!), isDirectory: true)
            .appendingPathComponent(String(format: "%02d", resilientComponents.day!), isDirectory: true)
        try FileManager.default.createDirectory(at: resilientCodexDay, withIntermediateDirectories: true)
        let resilientCodexURL = resilientCodexDay.appendingPathComponent("rollout.jsonl")
        var resilientCodexLine = Data(#"{"timestamp":"2026-07-15T06:25:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":55}}}}"#.utf8)
        resilientCodexLine.append(0x0a)
        try resilientCodexLine.write(to: resilientCodexURL)
        blockedOpenPath = resilientCodexURL.resolvingSymlinksInPath().path
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        let blockedBootstrapTokens = resilientService.snapshot().codex.tokensToday
        check(blockedBootstrapTokens == 0,
              "Codex bootstrap open failure preserves last-good (got \(blockedBootstrapTokens))")
        blockedOpenPath = nil
        resilientUptime += 15
        resilientService.refreshSynchronouslyForTesting()
        check(resilientService.snapshot().codex.tokensToday == 55,
              "Codex bootstrap open failure retries")

        let symbols = try StockMonitor.validatedSymbols([" qqq ", "BTCUSDT", "usQQQ", "tsla"])
        check(symbols == ["usQQQ", "BTCUSDT", "usTSLA"], "watchlist normalization")
        do {
            _ = try StockMonitor.validatedSymbols(["QQQ", "TSLA", "NVDA", "CRCL", "ETHBTC"])
            failures.append("watchlist limit")
        } catch {}
        let oldRow = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "QQQ", price: "717.14",
                                      pct: "+1.00%", up: 1)
        let stockPayload = StockMonitor.jsonData(rows: [oldRow], namesRev: 7, symbols: symbols,
                                                 updatedAt: 999_900, now: 1_000_000)
        if let object = try JSONSerialization.jsonObject(with: stockPayload) as? [String: Any] {
            check(object["symbols"] as? [String] == symbols, "stock payload symbol order")
            let payloadRows = object["stocks"] as? [[String: Any]]
            check(payloadRows?.first?["symbol"] as? String == "usQQQ", "stock payload row identity")
            check(object["updated_at"] as? Int == 999_900 && object["stale"] as? Bool == false,
                  "fresh stock payload timestamp")
        } else {
            failures.append("stock payload JSON")
        }
        let staleStockPayload = StockMonitor.jsonData(rows: [oldRow], namesRev: 7, symbols: symbols,
                                                      updatedAt: 999_800, now: 1_000_000)
        if let object = try JSONSerialization.jsonObject(with: staleStockPayload) as? [String: Any] {
            check(object["stale"] as? Bool == true, "stale stock payload timestamp")
        } else {
            failures.append("stale stock payload JSON")
        }
        check(StockMonitor.mergedRows(fresh: [:], previous: [oldRow], order: ["usTSLA"]).isEmpty,
              "removed stock row")
        check(StockMonitor.mergedRows(fresh: [:], previous: [oldRow], order: ["usQQQ"])
                .map(\.code) == ["QQQ"], "same stock fallback")
        check(StockMonitor.hasCompleteRefresh(fresh: ["usqqq": oldRow], order: ["usQQQ"]),
              "complete stock refresh")
        check(!StockMonitor.hasCompleteRefresh(fresh: ["usqqq": oldRow],
                                               order: ["usQQQ", "BTCUSDT"]),
              "partial stock refresh")
        let shanghai = StockMonitor.Row(symbol: "sh000001", code: "000001", name: "上证指数",
                                        price: "3500.00", pct: "+1.00%", up: 1)
        check(StockMonitor.mergedRows(fresh: [:], previous: [shanghai], order: ["sz000001"]).isEmpty,
              "market-qualified stock identity")

        let weatherNowData = Data(#"{"code":"200","now":{"obsTime":"2026-07-15T12:40+08:00","temp":"26","feelsLike":"23","icon":"307","text":"大雨","windSpeed":"43","humidity":"89"}}"#.utf8)
        let weatherDailyData = Data(#"{"code":"200","daily":[{"fxDate":"2026-07-15","sunrise":"05:51","sunset":"19:14","tempMax":"30","tempMin":"25"}]}"#.utf8)
        let airData = Data(#"{"indexes":[{"code":"us-epa","aqi":80,"category":"Moderate"},{"code":"cn-mee","aqi":41,"category":"优"}],"pollutants":[{"code":"pm2p5","concentration":{"value":12.0,"unit":"μg/m³"}}]}"#.utf8)
        let parsed = try WeatherMonitor.parse(nowData: weatherNowData, dailyData: weatherDailyData,
                                              airData: airData,
                                              now: Date(timeIntervalSince1970: 1_784_090_700))
        check(parsed.location == WeatherMonitor.locationName && parsed.conditionZh == "大雨" && parsed.sunrise == "05:51"
              && parsed.temperatureC == 26 && parsed.humidityPct == 89 && parsed.aqi == 41
              && parsed.highC == 30 && parsed.lowC == 25 && parsed.pm25 == 12,
              "QWeather weather parsing")
        check(WeatherMonitor.validAPIHost(WeatherMonitor.exampleAPIHost)
              && !WeatherMonitor.validAPIHost("qweatherapi.com.evil.example"),
              "QWeather API host validation")
        let initialText = WeatherMonitor().textRGB565()
        check(initialText.count == WeatherMonitor.textW * WeatherMonitor.textH * 2
              && initialText.contains(where: { $0 != 0 }), "weather text rendering")

        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "Asia/Shanghai")!
        let lunarDate = calendar.date(from: DateComponents(year: 2026, month: 7, day: 14, hour: 12))!
        check(WeatherMonitor.lunarText(for: lunarDate) == "农历六月初一", "lunar date")
        var localNow = lunarDate
        let lunarMonitor = WeatherMonitor(now: { localNow })
        localNow = calendar.date(from: DateComponents(year: 2026, month: 7, day: 15, hour: 0, minute: 1))!
        check(lunarMonitor.snapshot.lunarZh == "农历六月初二", "lunar rollover")
    } catch {
        failures.append("unexpected error: \(error)")
    }
    if failures.isEmpty {
        print("bridge self-test: ok")
        exit(0)
    }
    FileHandle.standardError.write(Data("bridge self-test failed: \(failures.joined(separator: ", "))\n".utf8))
    exit(1)
}

// Headless smoke test for the petdex -> GIF -> device pipeline (same code the
// pet picker window uses): AIClockBridge --test-pet <slug> <claude|codex> <host>
if CommandLine.arguments.count >= 4, CommandLine.arguments[1] == "--test-pet" {
    let slug = CommandLine.arguments[2]
    let slot = CommandLine.arguments[3]
    if CommandLine.arguments.count >= 5 { DeviceClient.host = CommandLine.arguments[4] }
    let size = slot == "claude" ? (w: 111, h: 120) : (w: 120, h: 120)
    let state = PetdexService.states.first { $0.id == "running" }!
    PetdexService.loadManifest { result in
        guard case let .success(pets) = result, let pet = pets.first(where: { $0.slug == slug }) else {
            print("manifest load failed or slug not found"); exit(1)
        }
        print("pet: \(pet.displayName) \(pet.spritesheetUrl)")
        PetdexService.downloadSpritesheet(pet) { result in
            guard case let .success(sheet) = result else { print("sheet download failed"); exit(1) }
            print("sheet: \(sheet.width)x\(sheet.height)")
            guard let gif = PetdexService.buildGif(sheet: sheet, state: state,
                                                   targetW: size.w, targetH: size.h) else {
                print("gif build failed"); exit(1)
            }
            print("gif: \(gif.count) bytes, uploading to \(DeviceClient.host) slot \(slot)...")
            DeviceClient.uploadGif(gif, slot: slot) { error in
                print(error.map { "upload failed: \($0.localizedDescription)" } ?? "upload ok")
                exit(error == nil ? 0 : 1)
            }
        }
    }
    RunLoop.main.run() // completions land on the main queue; exit() above ends us
    exit(0)
}

let port: UInt16 = 8765
let service = StatusService()
let usage = UsageFetcher()
service.usage = usage
let netMonitor = NetSpeedMonitor()
netMonitor.start()
let nowPlaying = NowPlayingMonitor()
nowPlaying.start()
service.musicPlayingProvider = { nowPlaying.snapshot.playing }

let stockMonitor = StockMonitor()
stockMonitor.start()
let weatherMonitor = WeatherMonitor()
weatherMonitor.start()

// Wired fallback: if the clock is plugged in over USB, push status/net down
// the serial line (works around AP client isolation; no WiFi setup needed).
let serialLink = SerialLink(service: service, netMonitor: netMonitor, stockMonitor: stockMonitor,
                            weatherMonitor: weatherMonitor)
serialLink.start()

let server = HTTPServer(port: port, routes: [
    "/": { service.snapshot().jsonData() },
    "/status": { service.snapshot().jsonData() },
    "/net": {
        let stats = SystemStatsMonitor.shared.snapshot()
        return netMonitor.jsonData(cpu: stats.cpu, mem: stats.mem)
    },
    "/music": { nowPlaying.jsonData() },
    "/stock": { stockMonitor.jsonData() },
    "/stock/config": { stockMonitor.configJSON() },
    "/weather": { weatherMonitor.jsonData() },
], binaryRoutes: [
    "/music/cover.raw": { nowPlaying.coverRGB565 },
    "/music/text.raw": { nowPlaying.textRGB565 },
    "/stock/names.raw": { stockMonitor.namesRGB565() },
    "/weather/text.raw": { weatherMonitor.textRGB565() },
], postRoutes: [
    // Claude Code / Codex hooks push lifecycle events here (see README §7):
    // curl -d '{"agent":"claude","event":"PreToolUse"}' http://127.0.0.1:8765/event
    "/event": { body in
        if let obj = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
           let agent = obj["agent"] as? String, let event = obj["event"] as? String {
            service.recordEvent(agent: agent, event: event, message: obj["message"] as? String)
            return Data("{\"ok\":true}".utf8)
        }
        return Data("{\"ok\":false}".utf8)
    },
    "/stock/config": { body in stockMonitor.updateConfig(json: body) },
])
server.authorizePost = { path, ip in
    DeviceClient.shouldAcceptBridgePost(path: path, ip: ip,
                                        deviceHost: DeviceClient.host,
                                        lastSeenIP: DeviceClient.lastSeenIP)
}
// Passive discovery: the clock polls us, so its source IP identifies it.
// Remember it (for auto-pairing / DHCP-change self-healing) and adopt it
// outright when no device is configured yet.
server.onRequest = { path, ip in
    guard DeviceClient.shouldRecordDevicePoll(path: path, ip: ip,
                                              localAddresses: DeviceClient.localIPv4Addresses()) else { return }
    DeviceClient.recordDevicePoll(ip: ip)
}
// Active fallback for when the passive route can't fire at all (fresh /
// erased device knows no bridge host, so it never polls anyone): if the
// device stays silent, find it ourselves and hand it our address.
Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { _ in
    DeviceClient.healPairingIfNeeded(port: port)
}

do {
    try server.start()
    FileHandle.standardError.write(Data("[bridge] serving /status on 0.0.0.0:\(port)\n".utf8))
    DeviceClient.verifyConfiguredDeviceIdentity { verified in
        if verified {
            FileHandle.standardError.write(Data("[pair] verified configured device identity\n".utf8))
        }
    }
} catch {
    FileHandle.standardError.write(Data("[bridge] failed to bind port \(port): \(error)\n".utf8))
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let menuBar = MenuBarController(service: service, usage: usage, netMonitor: netMonitor,
                                stockMonitor: stockMonitor, weatherMonitor: weatherMonitor, port: port)
_ = menuBar // retain
usage.startAutoRefresh()
app.run()
