import AppKit
import Foundation

enum MarketColorRole: Equatable {
    case gain
    case loss
    case neutral
}

func marketColorRole(up: Int) -> MarketColorRole {
    if up > 0 { return .gain }
    if up < 0 { return .loss }
    return .neutral
}

// Hybrid public quote feed: OKX spot tickers for crypto and Tencent for QQQ.
// Neither endpoint requires an API key:
//   https://www.okx.com/api/v5/market/ticker?instId=BTC-USDT
//   http://qt.gtimg.cn/q=usQQQ
// Polled every 5s; the device and the mirror both render the pre-formatted
// strings so the firmware stays dumb (and ASCII-only: it shows the code, the
// CJK company name is only used by the Mac mirror).
final class StockMonitor {
    enum ConfigurationError: LocalizedError {
        case empty
        case tooMany
        case invalidSymbol(String)

        var errorDescription: String? {
            switch self {
            case .empty:
                return "至少添加一个行情品种"
            case .tooMany:
                return "设备最多显示 4 个行情品种"
            case let .invalidSymbol(symbol):
                return "不支持的行情代码：\(symbol)"
            }
        }
    }

    struct Row {
        let symbol: String // canonical feed identity: "sh000001", "sz000001", "BTCUSDT"
        let code: String  // ASCII display code: "600519", "00700", "AAPL"
        let name: String  // CJK name, mirror only
        let price: String // pre-formatted
        let pct: String   // "+1.24%"
        let up: Int       // 1 rising / -1 falling / 0 flat

        init(symbol: String? = nil, code: String, name: String, price: String,
             pct: String, up: Int) {
            self.symbol = symbol ?? code
            self.code = code
            self.name = name
            self.price = price
            self.pct = pct
            self.up = up
        }
    }

    struct OKXMarket {
        let instrument: String
        let name: String
    }

    private final class FetchAccumulator: @unchecked Sendable {
        private let lock = NSLock()
        private var rows: [String: Row] = [:]

        func set(_ row: Row, for symbol: String) {
            lock.lock()
            rows[symbol.lowercased()] = row
            lock.unlock()
        }

        var snapshot: [String: Row] {
            lock.lock()
            defer { lock.unlock() }
            return rows
        }
    }

    private static let okxMarkets: [String: OKXMarket] = [
        "BTCUSDT": OKXMarket(instrument: "BTC-USDT", name: "BTC / USDT"),
        "ETHUSDT": OKXMarket(instrument: "ETH-USDT", name: "ETH / USDT"),
        "ETHBTC": OKXMarket(instrument: "ETH-BTC", name: "ETH / BTC"),
    ]

    static let symbolsKey = "stock_symbols"
    static let staleAfterSeconds = 120
    private static let symbolsSchemaKey = "stock_symbols_schema"
    private static let symbolsSchemaVersion = 1
    private static let defaultSymbols = "usQQQ,BTCUSDT,ETHUSDT,ETHBTC"
    /// Comma-separated display symbols. Three crypto pairs use OKX; everything
    /// else retains the existing Tencent prefix convention.
    static var symbols: [String] {
        get {
            migrateSymbolsIfNeeded()
            let raw = UserDefaults.standard.string(forKey: symbolsKey) ?? defaultSymbols
            return (try? validatedSymbols(splitSymbols(raw))) ?? splitSymbols(defaultSymbols)
        }
        set {
            guard let validated = try? validatedSymbols(newValue) else { return }
            UserDefaults.standard.set(validated.joined(separator: ","), forKey: symbolsKey)
        }
    }

    static func migrateSymbolsIfNeeded(defaults: UserDefaults = .standard) {
        guard defaults.integer(forKey: symbolsSchemaKey) < symbolsSchemaVersion else { return }
        defaults.set(defaultSymbols, forKey: symbolsKey)
        defaults.set(symbolsSchemaVersion, forKey: symbolsSchemaKey)
    }

    /// Market prefix must be lowercase but the US ticker must stay UPPERCASE
    /// ("usaapl" gets v_pv_none_match back), so normalize both halves.
    static func normalize(_ s: String) -> String {
        let trimmed = s.trimmingCharacters(in: .whitespacesAndNewlines)
        let upper = trimmed.uppercased()
        let compactCrypto = upper.replacingOccurrences(of: "-", with: "")
        if okxMarkets[compactCrypto] != nil { return compactCrypto }
        if trimmed.count > 2 {
            let prefix = trimmed.prefix(2).lowercased()
            if ["sh", "sz", "bj", "hk", "us"].contains(prefix) {
                return prefix + String(trimmed.dropFirst(2)).uppercased()
            }
        }
        return "us" + upper
    }

    static func validatedSymbols(_ input: [String]) throws -> [String] {
        var result: [String] = []
        var seen = Set<String>()
        for raw in input {
            let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else { continue }
            let symbol = normalize(trimmed)
            guard isSupported(symbol) else { throw ConfigurationError.invalidSymbol(trimmed) }
            let key = symbol.lowercased()
            guard seen.insert(key).inserted else { continue }
            result.append(symbol)
            if result.count > 4 { throw ConfigurationError.tooMany }
        }
        guard !result.isEmpty else { throw ConfigurationError.empty }
        return result
    }

    private static func splitSymbols(_ raw: String) -> [String] {
        raw.replacingOccurrences(of: "，", with: ",")
            .components(separatedBy: CharacterSet(charactersIn: ",\n"))
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
    }

    private static func isSupported(_ symbol: String) -> Bool {
        if okxMarkets[symbol.uppercased()] != nil { return true }
        guard symbol.count > 2,
              ["sh", "sz", "bj", "hk", "us"].contains(String(symbol.prefix(2))) else {
            return false
        }
        let code = String(symbol.dropFirst(2))
        return code.range(of: #"^[A-Z0-9][A-Z0-9.\-]{0,15}$"#,
                          options: .regularExpression) != nil
    }

    private let lock = NSLock()
    private var rows: [Row] = []
    private var updatedAt = 0
    private var timer: Timer?
    private let fetchStateLock = NSLock()
    private var fetchInFlight = false

    // The device's font is ASCII-only, so CJK company names go down as Mac-
    // rendered RGB565 strips (same trick as the music title strip): one
    // NAME_W x NAME_H strip per row, wire format [1 byte count][strips...].
    // names_rev in /stock tells the device when to re-fetch.
    static let nameW = 156, nameH = 16
    private var namesRev = 0
    private var namesData = Data([0])
    private var lastNamesKey = ""

    func start() {
        fetch()
        timer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.fetch()
        }
    }

    var snapshot: [Row] {
        lock.lock()
        defer { lock.unlock() }
        return rows
    }

    func jsonData() -> Data {
        lock.lock()
        let currentRows = rows
        let rev = namesRev
        let timestamp = updatedAt
        lock.unlock()
        return Self.jsonData(rows: currentRows, namesRev: rev, symbols: Self.symbols,
                             updatedAt: timestamp)
    }

    static func jsonData(rows: [Row], namesRev: Int, symbols: [String],
                         updatedAt: Int, now: Int = Int(Date().timeIntervalSince1970)) -> Data {
        let stocks = rows.map { r -> [String: Any] in
            ["symbol": r.symbol, "code": r.code, "name": r.name,
             "price": r.price, "pct": r.pct, "up": r.up]
        }
        let dict: [String: Any] = [
            "stocks": stocks,
            "symbols": symbols,
            "names_rev": namesRev,
            "updated_at": updatedAt,
            "stale": updatedAt <= 0 || now - updatedAt > staleAfterSeconds,
        ]
        return (try? JSONSerialization.data(withJSONObject: dict)) ?? Data("{}".utf8)
    }

    func configJSON() -> Data {
        Self.configResponse(ok: true, symbols: Self.symbols)
    }

    func updateConfig(json body: Data) -> Data {
        let values: [String]
        if let object = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
           let array = object["symbols"] as? [String] {
            values = array
        } else if let object = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
                  let raw = object["symbols"] as? String {
            values = Self.splitSymbols(raw)
        } else if let raw = String(data: body, encoding: .utf8), !raw.isEmpty {
            values = Self.splitSymbols(raw)
        } else {
            return Self.configResponse(ok: false, symbols: Self.symbols,
                                       error: "请求应包含 symbols 数组或逗号分隔字符串")
        }

        do {
            let validated = try Self.validatedSymbols(values)
            UserDefaults.standard.set(validated.joined(separator: ","), forKey: Self.symbolsKey)
            fetch()
            return Self.configResponse(ok: true, symbols: validated)
        } catch {
            return Self.configResponse(ok: false, symbols: Self.symbols,
                                       error: error.localizedDescription)
        }
    }

    @discardableResult
    func updateSymbols(_ values: [String]) -> Result<[String], Error> {
        do {
            let validated = try Self.validatedSymbols(values)
            UserDefaults.standard.set(validated.joined(separator: ","), forKey: Self.symbolsKey)
            fetch()
            return .success(validated)
        } catch {
            return .failure(error)
        }
    }

    private static func configResponse(ok: Bool, symbols: [String], error: String? = nil) -> Data {
        var object: [String: Any] = ["ok": ok, "symbols": symbols]
        if let error { object["error"] = error }
        return (try? JSONSerialization.data(withJSONObject: object)) ?? Data("{}".utf8)
    }

    /// [1 byte count][NAME_W x NAME_H RGB565 big-endian per row...]
    func namesRGB565() -> Data {
        lock.lock()
        defer { lock.unlock() }
        return namesData
    }

    private func scheduleNameRendering(_ parsed: [Row]) {
        if Thread.isMainThread {
            renderNamesIfNeeded(parsed)
        } else {
            DispatchQueue.main.async { [weak self] in self?.renderNamesIfNeeded(parsed) }
        }
    }

    private func renderNamesIfNeeded(_ parsed: [Row]) {
        precondition(Thread.isMainThread)
        let key = parsed.prefix(4).map { $0.name }.joined(separator: "\n")
        lock.lock()
        let dirty = key != lastNamesKey
        lock.unlock()
        guard dirty else { return }
        var data = Data([UInt8(min(parsed.count, 4))])
        for row in parsed.prefix(4) {
            data.append(Self.renderNameStrip(row.name) ?? Data(count: Self.nameW * Self.nameH * 2))
        }
        lock.lock()
        lastNamesKey = key
        namesData = data
        namesRev += 1
        lock.unlock()
    }

    private static func renderNameStrip(_ name: String) -> Data? {
        let w = nameW, h = nameH
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8,
                                  bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return nil
        }
        ctx.setFillColor(CGColor(red: 0, green: 0, blue: 0, alpha: 1))
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        let style = NSMutableParagraphStyle()
        style.alignment = .right // sits left of the row's right edge, code is on the left
        style.lineBreakMode = .byTruncatingTail
        (name as NSString).draw(in: NSRect(x: 0, y: 1, width: w, height: h - 1), withAttributes: [
            .font: NSFont.systemFont(ofSize: 12, weight: .medium),
            .foregroundColor: NSColor(white: 0.72, alpha: 1),
            .paragraphStyle: style,
        ])
        NSGraphicsContext.restoreGraphicsState()
        guard let rendered = ctx.data else { return nil }
        let px = rendered.bindMemory(to: UInt8.self, capacity: w * h * 4)
        var out = Data(capacity: w * h * 2)
        for i in 0..<(w * h) {
            let v = (UInt16(px[i * 4] & 0xF8) << 8) | (UInt16(px[i * 4 + 1] & 0xFC) << 3)
                | UInt16(px[i * 4 + 2] >> 3)
            out.append(UInt8((v >> 8) & 0xFF))
            out.append(UInt8(v & 0xFF))
        }
        return out
    }

    private func fetch() {
        fetchStateLock.lock()
        guard !fetchInFlight else {
            fetchStateLock.unlock()
            return
        }
        fetchInFlight = true
        fetchStateLock.unlock()

        let symbols = Self.symbols
        guard !symbols.isEmpty else {
            lock.lock(); rows = []; updatedAt = 0; lock.unlock()
            finishFetch()
            return
        }

        let accumulator = FetchAccumulator()
        let group = DispatchGroup()

        for symbol in symbols {
            guard let market = Self.okxMarkets[symbol.uppercased()],
                  var components = URLComponents(string: "https://www.okx.com/api/v5/market/ticker") else {
                continue
            }
            components.queryItems = [URLQueryItem(name: "instId", value: market.instrument)]
            guard let url = components.url else { continue }
            group.enter()
            var request = URLRequest(url: url)
            request.timeoutInterval = 5
            URLSession.shared.dataTask(with: request) { data, _, _ in
                defer { group.leave() }
                guard let data = data,
                      let row = Self.parseOKXTicker(data: data, symbol: symbol, market: market) else { return }
                accumulator.set(row, for: symbol)
            }.resume()
        }

        let tencentSymbols = symbols.filter { Self.okxMarkets[$0.uppercased()] == nil }
        if !tencentSymbols.isEmpty,
           let url = URL(string: "http://qt.gtimg.cn/q=" + tencentSymbols.joined(separator: ",")) {
            group.enter()
            var request = URLRequest(url: url)
            request.timeoutInterval = 5
            URLSession.shared.dataTask(with: request) { data, _, _ in
                defer { group.leave() }
                guard let data = data else { return }
                let gbk = String.Encoding(rawValue: CFStringConvertEncodingToNSStringEncoding(
                    CFStringEncoding(CFStringEncodings.GB_18030_2000.rawValue)))
                let text = String(data: data, encoding: gbk)
                    ?? String(data: data, encoding: .isoLatin1) ?? ""
                for row in Self.parseTencent(text: text, order: tencentSymbols) {
                    accumulator.set(row, for: row.symbol)
                }
            }.resume()
        }

        group.notify(queue: .global(qos: .utility)) { [weak self] in
            guard let self = self else { return }
            if Self.symbols == symbols {
                self.apply(fresh: accumulator.snapshot, order: symbols)
            }
            self.finishFetch()
        }
    }

    private func finishFetch() {
        fetchStateLock.lock()
        fetchInFlight = false
        fetchStateLock.unlock()
    }

    private func apply(fresh: [String: Row], order: [String]) {
        let complete = Self.hasCompleteRefresh(fresh: fresh, order: order)
        lock.lock()
        let merged = Self.mergedRows(fresh: fresh, previous: rows, order: order)
        rows = merged
        if complete { updatedAt = Int(Date().timeIntervalSince1970) }
        lock.unlock()
        scheduleNameRendering(merged)
    }

    static func mergedRows(fresh: [String: Row], previous: [Row], order: [String]) -> [Row] {
        var previousBySymbol: [String: Row] = [:]
        for row in previous { previousBySymbol[row.symbol.lowercased()] = row }
        return order.compactMap { symbol in
            fresh[symbol.lowercased()] ?? previousBySymbol[symbol.lowercased()]
        }
    }

    static func hasCompleteRefresh(fresh: [String: Row], order: [String]) -> Bool {
        !order.isEmpty && order.allSatisfy { fresh[$0.lowercased()] != nil }
    }

    static func parseOKXTicker(data: Data, symbol: String, market: OKXMarket) -> Row? {
        guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              root["code"] as? String == "0",
              let tickers = root["data"] as? [[String: Any]],
              let ticker = tickers.first,
              ticker["instId"] as? String == market.instrument,
              let lastText = ticker["last"] as? String,
              let openText = ticker["open24h"] as? String,
              let last = Double(lastText), let open = Double(openText), open > 0 else { return nil }
        let change = last - open
        let pct = change / open * 100
        return Row(symbol: symbol, code: symbol.uppercased(), name: market.name, price: formatPrice(last),
                   pct: String(format: "%+.2f%%", pct), up: change > 0 ? 1 : (change < 0 ? -1 : 0))
    }

    /// Response is lines of `v_sh600519="1~贵州茅台~600519~1212.00~...";`
    /// fields split by "~": [1]=name [3]=price [31]=change [32]=change%.
    static func parseTencent(text: String, order: [String]) -> [Row] {
        var bySymbol: [String: Row] = [:]
        for line in text.split(whereSeparator: { $0.isNewline }) {
            guard let eq = line.firstIndex(of: "="), line.hasPrefix("v_") else { continue }
            let symbol = String(line[line.index(line.startIndex, offsetBy: 2)..<eq]).lowercased()
            let f = line[line.index(after: eq)...]
                .trimmingCharacters(in: CharacterSet(charactersIn: "\";"))
                .components(separatedBy: "~")
            guard f.count > 32, let price = Double(f[3]), let chg = Double(f[31]),
                  let pct = Double(f[32]) else { continue }
            // lowercase key so the case-normalized query symbol still matches
            let canonical = order.first(where: { $0.lowercased() == symbol.lowercased() }) ?? symbol
            bySymbol[symbol.lowercased()] = Row(
                symbol: canonical,
                code: displayCode(symbol),
                name: f[1],
                price: formatPrice(price),
                pct: String(format: "%+.2f%%", pct),
                up: chg > 0 ? 1 : (chg < 0 ? -1 : 0))
        }
        return order.compactMap { bySymbol[$0.lowercased()] }
    }

    /// "sh600519" -> "600519", "usAAPL" -> "AAPL", "hk00700" -> "00700"
    static func displayCode(_ symbol: String) -> String {
        for p in ["sh", "sz", "bj", "hk", "us"] where symbol.hasPrefix(p) && symbol.count > 2 {
            return String(symbol.dropFirst(2)).uppercased()
        }
        return symbol.uppercased()
    }

    static func formatPrice(_ p: Double) -> String {
        if p >= 10000 { return String(format: "%.0f", p) }
        if p >= 1000 { return String(format: "%.1f", p) }
        if p >= 1 { return String(format: "%.2f", p) }
        if p >= 0.01 { return String(format: "%.5f", p) }
        return String(format: "%.8f", p)
    }
}
