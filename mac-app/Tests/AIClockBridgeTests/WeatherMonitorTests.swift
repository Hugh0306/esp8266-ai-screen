import XCTest
@testable import AIClockBridge

final class WeatherMonitorTests: XCTestCase {
    func testParsesQWeatherNowDailyAndAirQuality() throws {
        let nowData = Data(#"{"code":"200","now":{"obsTime":"2026-07-15T12:40+08:00","temp":"26","feelsLike":"23","icon":"307","text":"大雨","windSpeed":"43","humidity":"89"}}"#.utf8)
        let dailyData = Data(#"{"code":"200","daily":[{"fxDate":"2026-07-15","sunrise":"05:51","sunset":"19:14","tempMax":"30","tempMin":"25"}]}"#.utf8)
        let airData = Data(#"{"indexes":[{"code":"us-epa","aqi":80,"category":"Moderate"},{"code":"cn-mee","aqi":41,"category":"优"}],"pollutants":[{"code":"pm2p5","concentration":{"value":12.0}}]}"#.utf8)

        let weather = try WeatherMonitor.parse(nowData: nowData, dailyData: dailyData,
                                               airData: airData,
                                               now: Date(timeIntervalSince1970: 1_784_090_700))

        XCTAssertEqual(weather.location, WeatherMonitor.locationName)
        XCTAssertEqual(weather.temperatureC, 26)
        XCTAssertEqual(weather.apparentC, 23)
        XCTAssertEqual(weather.humidityPct, 89)
        XCTAssertEqual(weather.weatherCode, 65)
        XCTAssertEqual(weather.conditionZh, "大雨")
        XCTAssertEqual(weather.conditionEn, "Heavy rain")
        XCTAssertEqual(weather.skycon, "HEAVY_RAIN")
        XCTAssertEqual(weather.aqi, 41)
        XCTAssertEqual(weather.pm25, 12)
        XCTAssertEqual(weather.airQualityZh, "优")
        XCTAssertEqual(weather.windKmh, 43)
        XCTAssertEqual(weather.highC, 30)
        XCTAssertEqual(weather.lowC, 25)
        XCTAssertEqual(weather.sunrise, "05:51")
        XCTAssertEqual(weather.sunset, "19:14")
    }

    func testRejectsIncompleteQWeatherPayloadSet() {
        let invalid = Data(#"{"code":"200"}"#.utf8)
        XCTAssertThrowsError(try WeatherMonitor.parse(nowData: invalid, dailyData: invalid,
                                                      airData: invalid, now: Date()))
    }

    func testValidatesQWeatherCredentialsAndURLBoundary() throws {
        XCTAssertTrue(WeatherMonitor.validToken("weather_token_123"))
        XCTAssertFalse(WeatherMonitor.validToken("short"))
        XCTAssertFalse(WeatherMonitor.validToken("invalid token value"))
        XCTAssertTrue(WeatherMonitor.validAPIHost(WeatherMonitor.exampleAPIHost))
        XCTAssertFalse(WeatherMonitor.validAPIHost("qweatherapi.com.evil.example"))
        let url = try XCTUnwrap(WeatherMonitor.apiURL(
            host: WeatherMonitor.exampleAPIHost,
            path: "/v7/weather/now",
            queryItems: [URLQueryItem(name: "location", value: "116.4074,39.9042")]
        ))
        XCTAssertEqual(url.host, WeatherMonitor.exampleAPIHost)
        XCTAssertEqual(url.query?.contains("location=116.4074,39.9042"), true)
        XCTAssertFalse(url.absoluteString.contains("weather_token_123"))
    }

    func testFormatsChineseLunarDate() throws {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = try XCTUnwrap(TimeZone(identifier: "Asia/Shanghai"))
        let date = try XCTUnwrap(calendar.date(from: DateComponents(
            year: 2026, month: 7, day: 14, hour: 12
        )))

        XCTAssertEqual(WeatherMonitor.lunarText(for: date), "农历六月初一")
    }

    func testWeatherTextStripHasFixedRGB565Size() throws {
        let monitor = WeatherMonitor()

        XCTAssertEqual(monitor.textRGB565().count, WeatherMonitor.textW * WeatherMonitor.textH * 2)
    }

    func testLunarDateAdvancesEvenWithoutFreshWeather() throws {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = try XCTUnwrap(TimeZone(identifier: "Asia/Shanghai"))
        var current = try XCTUnwrap(calendar.date(from: DateComponents(
            year: 2026, month: 7, day: 14, hour: 23, minute: 59
        )))
        let monitor = WeatherMonitor(now: { current })
        current = try XCTUnwrap(calendar.date(from: DateComponents(
            year: 2026, month: 7, day: 15, hour: 0, minute: 1
        )))

        XCTAssertEqual(monitor.snapshot.lunarZh, "农历六月初二")
    }
}
