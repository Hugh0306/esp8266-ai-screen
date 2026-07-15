import AppKit

// Menu-bar item: a transparent market-monitor template icon that adapts to
// light/dark menu bars. Left click opens a live mirror of the ESP8266
// screen (MirrorPopover); right click opens the control menu with usage
// meters and device remote control. No quota text lives in the bar itself.
final class MenuBarController: NSObject, NSMenuDelegate {
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    private let service: StatusService
    private let usage: UsageFetcher
    private let port: UInt16
    private let stockMonitor: StockMonitor
    private let controlMenu = NSMenu()
    private let mirrorPopover: MirrorPopoverController

    private let claudeUsageItem = NSMenuItem(title: "Claude …", action: nil, keyEquivalent: "")
    private let codexUsageItem = NSMenuItem(title: "Codex …", action: nil, keyEquivalent: "")
    private let deviceInfoItem = NSMenuItem(title: "设备：未设置", action: nil, keyEquivalent: "")
    private var modeItems: [String: NSMenuItem] = [:]

    init(service: StatusService, usage: UsageFetcher, netMonitor: NetSpeedMonitor,
         stockMonitor: StockMonitor, weatherMonitor: WeatherMonitor, port: UInt16) {
        self.service = service
        self.usage = usage
        self.port = port
        self.stockMonitor = stockMonitor
        self.mirrorPopover = MirrorPopoverController(service: service, netMonitor: netMonitor,
                                                     stockMonitor: stockMonitor,
                                                     weatherMonitor: weatherMonitor)
        super.init()
        buildMenu()
        if let button = statusItem.button {
            button.image = Self.marketMonitorIcon()
            button.target = self
            button.action = #selector(statusItemClicked)
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }
    }

    /// Monochrome template artwork keeps the menu-bar background transparent
    /// and lets macOS choose the correct foreground color for each appearance.
    private static func marketMonitorIcon() -> NSImage {
        let size = NSSize(width: 18, height: 18)
        let img = NSImage(size: size, flipped: false) { _ in
            NSColor.black.setStroke()

            let monitor = NSBezierPath(roundedRect: NSRect(x: 1.5, y: 3.5, width: 15, height: 12),
                                       xRadius: 2, yRadius: 2)
            monitor.lineWidth = 1.35
            monitor.stroke()

            let chart = NSBezierPath()
            chart.move(to: NSPoint(x: 4, y: 7))
            chart.line(to: NSPoint(x: 7, y: 9.5))
            chart.line(to: NSPoint(x: 9.5, y: 8.2))
            chart.line(to: NSPoint(x: 14, y: 12.2))
            chart.lineWidth = 1.55
            chart.lineCapStyle = .round
            chart.lineJoinStyle = .round
            chart.stroke()

            let stand = NSBezierPath()
            stand.move(to: NSPoint(x: 7, y: 1.8))
            stand.line(to: NSPoint(x: 11, y: 1.8))
            stand.move(to: NSPoint(x: 9, y: 1.8))
            stand.line(to: NSPoint(x: 9, y: 3.5))
            stand.lineWidth = 1.35
            stand.lineCapStyle = .round
            stand.stroke()
            return true
        }
        img.isTemplate = true
        return img
    }

    /// Left click -> mirror popover; right click -> control menu.
    @objc private func statusItemClicked() {
        guard let button = statusItem.button else { return }
        let event = NSApp.currentEvent
        if event?.type == .rightMouseUp || event?.modifierFlags.contains(.control) == true {
            statusItem.menu = controlMenu
            button.performClick(nil)
            statusItem.menu = nil // detach so left click keeps toggling the popover
        } else {
            mirrorPopover.toggle(relativeTo: button)
        }
    }

    // MARK: - menu construction

    private func buildMenu() {
        let menu = controlMenu
        menu.delegate = self

        claudeUsageItem.isEnabled = false
        codexUsageItem.isEnabled = false
        menu.addItem(claudeUsageItem)
        menu.addItem(codexUsageItem)
        menu.addItem(.separator())

        deviceInfoItem.isEnabled = false
        menu.addItem(deviceInfoItem)

        menu.addItem(makeItem("自动查找并配对设备", #selector(autoPairAction)))
        menu.addItem(makeItem("设置设备地址…", #selector(setDeviceAddress)))
        menu.addItem(makeItem("打开设备网页", #selector(openDevicePage)))

        let displayMenu = NSMenu()
        for (title, mode) in [("自动（谁在干活显示谁）", "auto"), ("固定 Claude", "claude"),
                              ("固定 Codex", "codex"), ("网速曲线", "net"),
                              ("天气时钟", "weather"), ("股票行情", "stock")] {
            let item = NSMenuItem(title: title, action: #selector(setDisplayMode(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = mode
            modeItems[mode] = item
            displayMenu.addItem(item)
        }
        let displayItem = NSMenuItem(title: "屏幕显示", action: nil, keyEquivalent: "")
        displayItem.submenu = displayMenu
        menu.addItem(displayItem)
        // (屏幕亮度在左键弹出的镜像页底部，做成滑条了)

        menu.addItem(makeItem("设置行情品种…", #selector(setStockSymbols)))

        menu.addItem(makeItem("更换桌宠动画…（petdex）", #selector(openPetPicker)))

        let resetMenu = NSMenu()
        for (title, slot) in [("Claude 恢复默认", "claude"), ("Codex 恢复默认", "codex")] {
            let item = NSMenuItem(title: title, action: #selector(resetSprite(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = slot
            resetMenu.addItem(item)
        }
        let resetItem = NSMenuItem(title: "恢复默认动画", action: nil, keyEquivalent: "")
        resetItem.submenu = resetMenu
        menu.addItem(resetItem)

        menu.addItem(makeItem("把本机设为设备桥接", #selector(pointBridgeHere)))
        menu.addItem(.separator())
        menu.addItem(makeItem("刷新", #selector(refreshAction), key: "r"))
        menu.addItem(makeItem("桥接服务地址", #selector(showAddress)))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "退出", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
    }

    private func makeItem(_ title: String, _ action: Selector, key: String = "") -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.target = self
        return item
    }

    // MARK: - refresh

    func menuWillOpen(_ menu: NSMenu) {
        usage.refresh()
        refreshUsageLines()
        refreshDeviceSection()
    }

    private func refreshUsageLines() {
        claudeUsageItem.title = Self.usageLine(name: "Claude", u: usage.claude, weeklyLabel: "7天")
        codexUsageItem.title = Self.usageLine(name: "Codex", u: usage.codex, weeklyLabel: "周")
    }

    private static func usageLine(name: String, u: ProviderUsage, weeklyLabel: String) -> String {
        if let err = u.error, u.primaryPct == nil, u.weeklyPct == nil { return "\(name)：\(err)" }
        var parts: [String] = []
        if let p = u.primaryPct {
            var s = "5h \(Int(p))%"
            if let m = u.primaryResetMin { s += "（\(fmtMin(m))后重置）" }
            parts.append(s)
        }
        if let p = u.weeklyPct {
            var s = "\(weeklyLabel) \(Int(p))%"
            if let m = u.weeklyResetMin { s += "（\(fmtMin(m))）" }
            parts.append(s)
        }
        return parts.isEmpty ? "\(name)：额度未知" : "\(name)　" + parts.joined(separator: "　")
    }

    private static func fmtMin(_ min: Int) -> String {
        if min >= 48 * 60 { return "\(min / (24 * 60))天" }
        if min >= 60 { return "\(min / 60)h\(min % 60 > 0 ? "\(min % 60)m" : "")" }
        return "\(min)m"
    }

    private func refreshDeviceSection() {
        let host = DeviceClient.host
        guard !host.isEmpty else {
            deviceInfoItem.title = "设备：未设置地址"
            modeItems.values.forEach { $0.state = .off }
            return
        }
        deviceInfoItem.title = "设备：\(host)（连接中…）"
        DeviceClient.fetchInfo { [weak self] result in
            guard let self = self else { return }
            switch result {
            case let .success(info):
                let sprites = [info.claudeCustomSprite ? "C:自定义" : "C:默认",
                               info.codexCustomSprite ? "X:自定义" : "X:默认"]
                let showing = info.mode == "net" ? "网速"
                    : (info.mode == "weather" || info.mode == "music") ? "天气"
                    : info.mode == "stock" ? "股票"
                    : (info.showing == "claude" ? "Claude" : "Codex")
                self.deviceInfoItem.title =
                    "设备：\(info.ip) · 正在显示 \(showing) · \(sprites.joined(separator: " "))"
                for (mode, item) in self.modeItems {
                    let selected = mode == info.mode || (mode == "weather" && info.mode == "music")
                    item.state = selected ? .on : .off
                }
            case .failure:
                self.deviceInfoItem.title = "设备：\(host)（无法连接）"
                self.modeItems.values.forEach { $0.state = .off }
            }
        }
    }

    // MARK: - pairing

    @objc private func autoPairAction() {
        deviceInfoItem.title = "设备：正在查找…"
        DeviceClient.autoPair(progress: { [weak self] msg in
            self?.deviceInfoItem.title = "设备：\(msg)"
        }, completion: { [weak self] result in
            switch result {
            case let .paired(ip):
                Self.toast("配对成功", "已找到设备并配对：\(ip)")
                self?.refreshDeviceSection()
            case .notFound:
                Self.toast("未找到设备", """
                局域网内没有发现 ESP8266 时钟。请确认：
                1. 设备已通电并连上同一个 WiFi（首次使用需通过 AI-Clock-Setup 热点配网）
                2. 路由器未开启"客户端隔离"
                """)
                self?.refreshDeviceSection()
            case .cancelled:
                self?.refreshDeviceSection()
            }
        })
    }

    // MARK: - actions

    @objc private func refreshAction() {
        usage.refresh()
        refreshUsageLines()
        refreshDeviceSection()
    }

    @objc private func setDeviceAddress() {
        let alert = NSAlert()
        alert.messageText = "设备地址"
        alert.informativeText = "ESP8266 时钟的 IP（设备开机时屏幕上会显示，例如 192.0.2.10）"
        let input = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24))
        input.stringValue = DeviceClient.host
        input.placeholderString = "192.0.2.10"
        alert.accessoryView = input
        alert.addButton(withTitle: "保存")
        alert.addButton(withTitle: "取消")
        NSApp.activate(ignoringOtherApps: true)
        if alert.runModal() == .alertFirstButtonReturn {
            DeviceClient.host = input.stringValue.trimmingCharacters(in: .whitespaces)
            refreshDeviceSection()
        }
    }

    @objc private func openDevicePage() {
        guard let url = DeviceClient.baseURL else {
            setDeviceAddress()
            return
        }
        NSWorkspace.shared.open(url)
    }

    @objc private func setDisplayMode(_ sender: NSMenuItem) {
        guard let mode = sender.representedObject as? String else { return }
        DeviceClient.setDisplayMode(mode) { [weak self] error in
            if let error = error {
                Self.toast("切换失败", error.localizedDescription)
            } else {
                self?.refreshDeviceSection()
            }
        }
    }

    @objc private func setStockSymbols() {
        let alert = NSAlert()
        alert.messageText = "行情品种"
        alert.informativeText = "BTCUSDT、ETHUSDT、ETHBTC 使用 OKX；QQQ、TSLA 等美股使用腾讯。\n逗号分隔，去重后最多 4 个。"
        let input = NSTextField(frame: NSRect(x: 0, y: 0, width: 280, height: 24))
        input.stringValue = StockMonitor.symbols.joined(separator: ",")
        input.placeholderString = "usQQQ,BTCUSDT,ETHUSDT,ETHBTC"
        alert.accessoryView = input
        alert.addButton(withTitle: "保存")
        alert.addButton(withTitle: "取消")
        NSApp.activate(ignoringOtherApps: true)
        if alert.runModal() == .alertFirstButtonReturn {
            let values = input.stringValue.replacingOccurrences(of: "，", with: ",")
                .split(separator: ",")
                .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            if case let .failure(error) = stockMonitor.updateSymbols(values) {
                Self.toast("保存失败", error.localizedDescription)
            }
        }
    }

    @objc private func openPetPicker() {
        if DeviceClient.host.isEmpty { setDeviceAddress() }
        PetPickerWindowController.shared.show()
    }

    @objc private func resetSprite(_ sender: NSMenuItem) {
        guard let slot = sender.representedObject as? String else { return }
        DeviceClient.resetSprite(slot: slot) { [weak self] error in
            if let error = error {
                Self.toast("恢复失败", error.localizedDescription)
            } else {
                self?.refreshDeviceSection()
            }
        }
    }

    @objc private func pointBridgeHere() {
        guard let ip = DeviceClient.localIPv4() else {
            Self.toast("失败", "获取本机局域网 IP 失败")
            return
        }
        let bridge = "\(ip):\(port)"
        DeviceClient.setBridgeHost(bridge) { error in
            if let error = error {
                Self.toast("设置失败", error.localizedDescription)
            } else {
                Self.toast("已设置", "设备将从 http://\(bridge)/status 拉取状态")
            }
        }
    }

    @objc private func showAddress() {
        let ip = DeviceClient.localIPv4() ?? "<本机局域网IP>"
        Self.toast("桥接服务地址", "http://\(ip):\(port)/status\n\n设备端 Bridge host 填：\(ip):\(port)")
    }

    private static func toast(_ title: String, _ text: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = text
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }
}
