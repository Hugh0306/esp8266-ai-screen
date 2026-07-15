import XCTest
@testable import AIClockBridge

final class StockMonitorTests: XCTestCase {
    func testMarketColorRoleUsesGreenForGainsAndRedForLosses() {
        XCTAssertEqual(marketColorRole(up: 1), .gain)
        XCTAssertEqual(marketColorRole(up: -1), .loss)
        XCTAssertEqual(marketColorRole(up: 0), .neutral)
    }

    func testValidatesWatchlistByNormalizingDeduplicatingAndPreservingOrder() throws {
        let symbols = try StockMonitor.validatedSymbols([
            " qqq ", "BTCUSDT", "usQQQ", "tsla", "ETHBTC",
        ])

        XCTAssertEqual(symbols, ["usQQQ", "BTCUSDT", "usTSLA", "ETHBTC"])
    }

    func testRejectsWatchlistWithMoreThanFourUniqueSymbols() {
        XCTAssertThrowsError(try StockMonitor.validatedSymbols([
            "QQQ", "TSLA", "NVDA", "CRCL", "ETHBTC",
        ]))
    }

    func testRejectsInvalidWatchlistSymbol() {
        XCTAssertThrowsError(try StockMonitor.validatedSymbols(["QQQ", "bad symbol!"]))
    }

    func testStockPayloadIncludesNormalizedSymbolsInConfiguredOrder() throws {
        let symbols = try StockMonitor.validatedSymbols([" qqq ", "ETH-BTC", "BTCUSDT"])
        let row = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "纳指100ETF",
                                   price: "717.14", pct: "+1.00%", up: 1)

        let data = StockMonitor.jsonData(rows: [row], namesRev: 7, symbols: symbols,
                                         updatedAt: 1_000, now: 1_100)
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let stocks = try XCTUnwrap(object["stocks"] as? [[String: Any]])

        XCTAssertEqual(object["symbols"] as? [String], ["usQQQ", "ETHBTC", "BTCUSDT"])
        XCTAssertEqual(object["names_rev"] as? Int, 7)
        XCTAssertEqual(object["updated_at"] as? Int, 1_000)
        XCTAssertEqual(object["stale"] as? Bool, false)
        XCTAssertEqual(stocks.first?["symbol"] as? String, "usQQQ")
    }

    func testStockPayloadBecomesStaleWithoutACompleteRefresh() throws {
        let row = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "纳指100ETF",
                                   price: "717.14", pct: "+1.00%", up: 1)

        let data = StockMonitor.jsonData(rows: [row], namesRev: 1, symbols: ["usQQQ"],
                                         updatedAt: 1_000, now: 1_121)
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(object["stale"] as? Bool, true)
    }

    func testMergeDropsRowsRemovedFromConfigurationWhenRefreshFails() {
        let old = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "纳指100ETF", price: "717.14",
                                   pct: "+1.00%", up: 1)

        let rows = StockMonitor.mergedRows(fresh: [:], previous: [old], order: ["usTSLA"])

        XCTAssertTrue(rows.isEmpty)
    }

    func testMergeKeepsSameSymbolPreviousValueWhenRefreshFails() {
        let old = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "纳指100ETF", price: "717.14",
                                   pct: "+1.00%", up: 1)

        let rows = StockMonitor.mergedRows(fresh: [:], previous: [old], order: ["usQQQ"])

        XCTAssertEqual(rows.map(\.code), ["QQQ"])
    }

    func testMergeDoesNotReuseSameDisplayCodeFromAnotherMarket() {
        let shanghai = StockMonitor.Row(symbol: "sh000001", code: "000001", name: "上证指数",
                                        price: "3500.00", pct: "+1.00%", up: 1)

        let rows = StockMonitor.mergedRows(fresh: [:], previous: [shanghai], order: ["sz000001"])

        XCTAssertTrue(rows.isEmpty)
    }

    func testOnlyACompleteRefreshCanAdvanceFreshness() {
        let qqq = StockMonitor.Row(symbol: "usQQQ", code: "QQQ", name: "QQQ",
                                   price: "717.14", pct: "+1.00%", up: 1)

        XCTAssertTrue(StockMonitor.hasCompleteRefresh(fresh: ["usqqq": qqq], order: ["usQQQ"]))
        XCTAssertFalse(StockMonitor.hasCompleteRefresh(
            fresh: ["usqqq": qqq], order: ["usQQQ", "BTCUSDT"]))
    }

    func testMigratesExistingWatchlistOnce() throws {
        let suiteName = UUID().uuidString
        let suite = try XCTUnwrap(UserDefaults(suiteName: suiteName))
        defer { suite.removePersistentDomain(forName: suiteName) }
        suite.set("usTSLA,usNVDA", forKey: StockMonitor.symbolsKey)

        StockMonitor.migrateSymbolsIfNeeded(defaults: suite)
        XCTAssertEqual(suite.string(forKey: StockMonitor.symbolsKey), "usQQQ,BTCUSDT,ETHUSDT,ETHBTC")

        suite.set("usQQQ", forKey: StockMonitor.symbolsKey)
        StockMonitor.migrateSymbolsIfNeeded(defaults: suite)
        XCTAssertEqual(suite.string(forKey: StockMonitor.symbolsKey), "usQQQ")
    }

    func testParsesOKXTicker() throws {
        let data = Data(#"{"code":"0","data":[{"instId":"ETH-BTC","last":"0.031245","open24h":"0.030000"}]}"#.utf8)
        let market = StockMonitor.OKXMarket(instrument: "ETH-BTC", name: "ETH / BTC")

        let row = try XCTUnwrap(StockMonitor.parseOKXTicker(data: data, symbol: "ETHBTC", market: market))

        XCTAssertEqual(row.code, "ETHBTC")
        XCTAssertEqual(row.name, "ETH / BTC")
        XCTAssertEqual(row.price, "0.03125")
        XCTAssertEqual(row.pct, "+4.15%")
        XCTAssertEqual(row.up, 1)
    }

    func testParsesTencentTicker() throws {
        var fields = Array(repeating: "", count: 33)
        fields[1] = "纳指100ETF"
        fields[3] = "717.14"
        fields[31] = "-8.37"
        fields[32] = "-1.15"
        let text = "v_usQQQ=\"\(fields.joined(separator: "~"))\";"

        let row = try XCTUnwrap(StockMonitor.parseTencent(text: text, order: ["usQQQ"]).first)

        XCTAssertEqual(row.code, "QQQ")
        XCTAssertEqual(row.name, "纳指100ETF")
        XCTAssertEqual(row.price, "717.14")
        XCTAssertEqual(row.pct, "-1.15%")
        XCTAssertEqual(row.up, -1)
    }
}
