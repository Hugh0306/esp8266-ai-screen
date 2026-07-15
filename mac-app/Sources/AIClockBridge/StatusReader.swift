import Foundation

// Port of the old bridge.py log-reading logic. No account APIs / keys are
// touched - everything comes from the JSONL session logs Claude Code and Codex
// CLI already write to disk:
//   ~/.claude/projects/**/*.jsonl   (Claude Code transcripts)
//   ~/.codex/sessions/**/*.jsonl    (Codex CLI rollouts, incl. rate_limits)

struct ClaudeStatus {
    var status: String = "offline"
    var tokensToday: Int = 0
    var sessionMin: Int = 0
    var sessionWindowMin: Int = 300
    var fiveHourPct: Double? = nil
    var fiveHourResetMin: Int? = nil
    var sevenDayPct: Double? = nil
    var sevenDayResetMin: Int? = nil
    var needsInput: Bool = false // waiting on a permission/approval prompt
}

struct CodexStatus {
    var status: String = "offline"
    var tokensToday: Int = 0
    var primaryPct: Double? = nil
    var primaryWindowMin: Int? = nil
    var primaryResetMin: Int? = nil
    var weeklyPct: Double? = nil
    var weeklyWindowMin: Int? = nil
    var weeklyResetMin: Int? = nil
    var needsInput: Bool = false
}

struct Snapshot {
    var claude: ClaudeStatus
    var codex: CodexStatus
    var ts: Int
    var musicPlaying: Bool = false
}

/// Reads logs into a background incremental index; snapshots only read last-good state.
final class StatusService {
    struct DebugMetrics {
        let claudeBytesRead: Int
        let codexBytesRead: Int
        let scanCount: Int
    }

    private struct FileCursor {
        var inode: UInt64
        var size: UInt64
        var offset: UInt64
        var partialLine = Data()
    }

    private struct ClaudeFileIndex {
        var cursor: FileCursor
        var mtime: TimeInterval
        var tokensToday = 0
        var usageEpochs: [TimeInterval] = []
    }

    private struct CodexFileIndex {
        var cursor: FileCursor
        var mtime: TimeInterval
        var totalTokens = 0
        var rateLimits: [String: Any]?
        var rateLimitsTs: TimeInterval = 0
    }

    private struct FileMetadata {
        let url: URL
        let inode: UInt64
        let size: UInt64
        let mtime: TimeInterval
    }

    private struct RoundMetrics {
        var claudeBytesRead = 0
        var codexBytesRead = 0
    }

    private static let claudeUsageMarker = Data("\"usage\":{".utf8)
    private static let codexTokenMarker = Data("\"token_count\"".utf8)
    private static let readChunkSize = 256 * 1024

    private let claudeDir: URL
    private let codexDir: URL
    private let nowProvider: () -> TimeInterval
    private let uptimeProvider: () -> TimeInterval
    private let refreshInterval: TimeInterval
    private let openFileProvider: (URL) throws -> FileHandle
    private let readChunkProvider: (FileHandle, Int) throws -> Data?
    private let refreshQueue = DispatchQueue(label: "AIClockBridge.status-index", qos: .utility)

    /// Real OAuth quota (5h/weekly windows) merged into snapshots when set;
    /// log-derived values remain the fallback for offline use.
    var usage: UsageFetcher?

    /// Whether audio is playing right now (drives the device's AUTO -> music
    /// auto-switch). Set from NowPlayingMonitor in main.
    var musicPlayingProvider: (() -> Bool)?

    // Hook-pushed live state (POST /event from Claude Code / Codex hooks).
    // Events beat the mtime heuristic while fresh: "working" for up to 10min
    // (a long tool run emits nothing between PreToolUse and PostToolUse),
    // "idle" for 60s (long enough to kill the mtime tail after Stop, short
    // enough that a session without hooks isn't stuck idle).
    private struct AgentEvent {
        let state: String // "working" | "idle"
        let at: TimeInterval
    }

    private var claudeEvent: AgentEvent?
    private var codexEvent: AgentEvent?
    // "needs input": a permission/approval prompt is on screen, waiting on the
    // user. Set by an attention event, cleared by the next concrete lifecycle
    // event (the prompt got answered) or by TTL.
    private var claudeNeedsInputAt: TimeInterval?
    private var codexNeedsInputAt: TimeInterval?
    private let workingEventTTL: TimeInterval = 10 * 60
    private let idleEventTTL: TimeInterval = 60
    private let needsInputTTL: TimeInterval = 5 * 60

    private static let workingEvents: Set<String> = [
        "UserPromptSubmit", "PreToolUse", "PostToolUse", "SubagentStart", "SubagentStop",
        "PreCompact", "PostCompact", "WorktreeCreate",
    ]
    private static let idleEvents: Set<String> = [
        "Stop", "SessionEnd", "SessionStart",
    ]
    // Codex PermissionRequest and MCP Elicitation are always a real "act now"
    // prompt. Claude's Notification is broader — it also fires on task
    // completion / 60s-idle — so it only counts as needs-input when its
    // message is actually a permission request (see isPermissionNotification).
    private static let attentionEvents: Set<String> = [
        "Elicitation", "PermissionRequest",
    ]

    private func isPermissionNotification(_ message: String?) -> Bool {
        guard let m = message?.lowercased() else { return false }
        return m.contains("permission") || m.contains("approve") || m.contains("approval")
    }

    /// Called by the /event endpoint. Unknown event names are ignored.
    /// `message` is only sent for Claude's Notification hook.
    func recordEvent(agent: String, event: String, message: String? = nil) {
        stateLock.lock()
        defer { stateLock.unlock() }
        let now = nowProvider()
        // Claude Notification: flash only for permission prompts, not for
        // "task done / waiting for your input" notifications.
        if event == "Notification" {
            if isPermissionNotification(message) {
                if agent == "claude" { claudeNeedsInputAt = now }
                else if agent == "codex" { codexNeedsInputAt = now }
            }
            return
        }
        if Self.attentionEvents.contains(event) {
            if agent == "claude" { claudeNeedsInputAt = now }
            else if agent == "codex" { codexNeedsInputAt = now }
            return
        }
        let state: String
        if Self.workingEvents.contains(event) { state = "working" }
        else if Self.idleEvents.contains(event) { state = "idle" }
        else { return }
        let ev = AgentEvent(state: state, at: now)
        // any concrete lifecycle event means the prompt (if any) was answered
        if agent == "claude" { claudeEvent = ev; claudeNeedsInputAt = nil }
        else if agent == "codex" { codexEvent = ev; codexNeedsInputAt = nil }
    }

    private func needsInput(_ at: TimeInterval?, now: TimeInterval) -> Bool {
        guard let at = at else { return false }
        return now - at < needsInputTTL
    }

    /// Event override, applied on top of the log-derived status. "offline"
    /// from logs is only upgraded by a fresh working event (a live hook means
    /// the CLI is definitely running).
    private func overrideStatus(_ logStatus: String, with event: AgentEvent?, now: TimeInterval) -> String {
        guard let ev = event else { return logStatus }
        let age = now - ev.at
        if ev.state == "working", age < workingEventTTL { return "working" }
        if ev.state == "idle", age < idleEventTTL, logStatus == "working" { return "idle" }
        return logStatus
    }

    private let workingThreshold: TimeInterval = 20        // log touched within this -> "working"
    private let idleThreshold: TimeInterval = 30 * 60      // within this -> "idle", else "offline"
    private let stateLock = NSLock()
    private var lastGood: Snapshot
    private var completedUptime: TimeInterval?
    private var refreshInFlight = false
    private var completedScans = 0
    private var lastRoundMetrics = RoundMetrics()

    // Queue-confined index state. Log IO never runs while stateLock is held.
    private var indexedDayStart: TimeInterval?
    private var claudeIndexes: [String: ClaudeFileIndex] = [:]
    private var codexIndexes: [String: CodexFileIndex] = [:]

    private let isoFrac: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return f
    }()
    private let isoPlain: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime]
        return f
    }()

    init(
        claudeDir: URL = URL(fileURLWithPath: ("~/.claude/projects" as NSString).expandingTildeInPath,
                             isDirectory: true),
        codexDir: URL = URL(fileURLWithPath: ("~/.codex/sessions" as NSString).expandingTildeInPath,
                            isDirectory: true),
        now: @escaping () -> TimeInterval = { Date().timeIntervalSince1970 },
        uptime: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime },
        refreshInterval: TimeInterval = 15,
        openFile: @escaping (URL) throws -> FileHandle = { try FileHandle(forReadingFrom: $0) },
        readChunk: @escaping (FileHandle, Int) throws -> Data? = { try $0.read(upToCount: $1) }
    ) {
        self.claudeDir = claudeDir
        self.codexDir = codexDir
        self.nowProvider = now
        self.uptimeProvider = uptime
        self.refreshInterval = max(0, refreshInterval)
        self.openFileProvider = openFile
        self.readChunkProvider = readChunk
        let epoch = now()
        self.lastGood = Snapshot(claude: ClaudeStatus(), codex: CodexStatus(), ts: Int(epoch))
    }

    var debugMetrics: DebugMetrics {
        stateLock.lock()
        defer { stateLock.unlock() }
        return DebugMetrics(claudeBytesRead: lastRoundMetrics.claudeBytesRead,
                            codexBytesRead: lastRoundMetrics.codexBytesRead,
                            scanCount: completedScans)
    }

    func snapshot() -> Snapshot {
        let now = nowProvider()
        let uptime = uptimeProvider()
        var shouldRefresh = false
        stateLock.lock()
        var snap = lastGood
        if (completedUptime.map { uptime - $0 >= refreshInterval } ?? true), !refreshInFlight {
            refreshInFlight = true
            shouldRefresh = true
        }
        let currentClaudeEvent = claudeEvent
        let currentCodexEvent = codexEvent
        let currentClaudeNeedsInputAt = claudeNeedsInputAt
        let currentCodexNeedsInputAt = codexNeedsInputAt
        stateLock.unlock()

        if shouldRefresh {
            refreshQueue.async { [weak self] in self?.refreshAndPublish() }
        }
        snap.ts = Int(now)

        // overlays are cheap and applied on every call, so hook events and
        // fresh quota show through instantly even while the log scan is cached
        if let u = usage {
            let claudeUsage = u.claude
            snap.claude.fiveHourPct = claudeUsage.primaryPct
            snap.claude.fiveHourResetMin = claudeUsage.primaryResetMin
            snap.claude.sevenDayPct = claudeUsage.weeklyPct
            snap.claude.sevenDayResetMin = claudeUsage.weeklyResetMin
            let codexUsage = u.codex
            if let pct = codexUsage.primaryPct {
                snap.codex.primaryPct = pct
                snap.codex.primaryResetMin = codexUsage.primaryResetMin
            }
            if let pct = codexUsage.weeklyPct {
                snap.codex.weeklyPct = pct
                snap.codex.weeklyResetMin = codexUsage.weeklyResetMin
            }
        }
        snap.claude.status = overrideStatus(snap.claude.status, with: currentClaudeEvent, now: now)
        snap.codex.status = overrideStatus(snap.codex.status, with: currentCodexEvent, now: now)
        snap.claude.needsInput = needsInput(currentClaudeNeedsInputAt, now: now)
        snap.codex.needsInput = needsInput(currentCodexNeedsInputAt, now: now)
        snap.musicPlaying = musicPlayingProvider?() ?? false
        return snap
    }

    /// Deterministic seam for the executable self-test. Production callers use snapshot().
    func refreshSynchronouslyForTesting() {
        refreshQueue.sync { refreshAndPublish() }
    }

    private func refreshAndPublish() {
        let now = nowProvider()
        let todayStart = startOfDay(now)
        stateLock.lock()
        let previous = lastGood
        stateLock.unlock()
        if indexedDayStart != todayStart {
            indexedDayStart = todayStart
            claudeIndexes.removeAll(keepingCapacity: true)
            codexIndexes.removeAll(keepingCapacity: true)
        }

        var metrics = RoundMetrics()
        let snapshot = Snapshot(
            claude: readClaude(now: now, todayStart: todayStart, metrics: &metrics) ?? previous.claude,
            codex: readCodex(now: now, metrics: &metrics) ?? previous.codex,
            ts: Int(now)
        )
        let completed = uptimeProvider()
        stateLock.lock()
        lastGood = snapshot
        completedUptime = completed
        refreshInFlight = false
        completedScans += 1
        lastRoundMetrics = metrics
        stateLock.unlock()
    }

    // MARK: - helpers

    private func statusFromDelta(_ delta: TimeInterval) -> String {
        if delta < workingThreshold { return "working" }
        if delta < idleThreshold { return "idle" }
        return "offline"
    }

    private func parseISO(_ s: String?) -> Double? {
        guard let s = s else { return nil }
        if let d = isoFrac.date(from: s) { return d.timeIntervalSince1970 }
        if let d = isoPlain.date(from: s) { return d.timeIntervalSince1970 }
        return nil
    }

    private func startOfDay(_ epoch: TimeInterval) -> TimeInterval {
        Calendar.current.startOfDay(for: Date(timeIntervalSince1970: epoch)).timeIntervalSince1970
    }

    private func metadata(for url: URL) -> FileMetadata? {
        guard let attrs = try? FileManager.default.attributesOfItem(atPath: url.path),
              attrs[.type] as? FileAttributeType == .typeRegular,
              let size = (attrs[.size] as? NSNumber)?.uint64Value,
              let mtime = (attrs[.modificationDate] as? Date)?.timeIntervalSince1970 else { return nil }
        return FileMetadata(url: url,
                            inode: (attrs[.systemFileNumber] as? NSNumber)?.uint64Value ?? 0,
                            size: size, mtime: mtime)
    }

    private func jsonlFiles(in root: URL, recursive: Bool) -> [FileMetadata]? {
        let fm = FileManager.default
        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: root.path, isDirectory: &isDirectory) else { return [] }
        guard isDirectory.boolValue else { return nil }
        let urls: [URL]
        if recursive {
            var enumerationFailed = false
            guard let enumerator = fm.enumerator(at: root, includingPropertiesForKeys: nil,
                                                 options: [.skipsHiddenFiles],
                                                 errorHandler: { _, _ in
                                                     enumerationFailed = true
                                                     return false
                                                 }) else { return nil }
            urls = enumerator.compactMap { $0 as? URL }
            if enumerationFailed { return nil }
        } else {
            do {
                urls = try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil,
                                                  options: [.skipsHiddenFiles])
            } catch {
                return nil
            }
        }
        var files: [FileMetadata] = []
        for url in urls where url.pathExtension == "jsonl" {
            guard let value = metadata(for: url) else { return nil }
            files.append(value)
        }
        return files.sorted { $0.url.path < $1.url.path }
    }

    private func resetCursorIfNeeded(_ cursor: inout FileCursor, metadata: FileMetadata) -> Bool {
        guard cursor.inode == metadata.inode, metadata.size >= cursor.offset,
              metadata.size >= cursor.size else {
            cursor = FileCursor(inode: metadata.inode, size: metadata.size, offset: 0)
            return true
        }
        return false
    }

    private func containsRawMarker(_ data: Data, range: Range<Int>, marker: Data) -> Bool {
        guard marker.count > 0, range.count >= marker.count else { return false }
        return data.withUnsafeBytes { rawData in
            marker.withUnsafeBytes { rawMarker in
                let bytes = rawData.bindMemory(to: UInt8.self)
                let needle = rawMarker.bindMemory(to: UInt8.self)
                for start in range.lowerBound...(range.upperBound - marker.count) {
                    var matched = true
                    for i in 0..<marker.count where bytes[start + i] != needle[i] {
                        matched = false
                        break
                    }
                    if matched { return true }
                }
                return false
            }
        }
    }

    private func readAppended(
        _ metadata: FileMetadata,
        cursor: inout FileCursor,
        bytesRead: inout Int,
        line: (Data, Range<Int>) -> Void
    ) -> Bool {
        guard cursor.offset < metadata.size else {
            cursor.size = metadata.size
            return true
        }
        let originalCursor = cursor
        guard let handle = try? openFileProvider(metadata.url) else { return false }
        defer { try? handle.close() }
        var roundBytesRead = 0
        do {
            try handle.seek(toOffset: cursor.offset)
            var buffer = cursor.partialLine
            while cursor.offset < metadata.size {
                let wanted = Int(min(UInt64(Self.readChunkSize), metadata.size - cursor.offset))
                guard let chunk = try readChunkProvider(handle, wanted), !chunk.isEmpty else {
                    cursor = originalCursor
                    return false
                }
                let oldCount = buffer.count
                buffer.append(chunk)
                cursor.offset += UInt64(chunk.count)
                roundBytesRead += chunk.count

                var lineStart = 0
                if buffer.count > oldCount {
                    for index in oldCount..<buffer.count where buffer[index] == 0x0a {
                        if index > lineStart { line(buffer, lineStart..<index) }
                        lineStart = index + 1
                    }
                }
                if lineStart > 0 { buffer.removeSubrange(0..<lineStart) }
                if chunk.count < wanted { break }
            }
            cursor.partialLine = buffer
            cursor.size = metadata.size
            bytesRead += roundBytesRead
            return true
        } catch {
            cursor = originalCursor
            return false
        }
    }

    private func intVal(_ any: Any?) -> Int {
        (any as? NSNumber)?.intValue ?? 0
    }

    // MARK: - Claude

    private func readClaude(now: TimeInterval, todayStart: TimeInterval,
                            metrics: inout RoundMetrics) -> ClaudeStatus? {
        let originalIndexes = claudeIndexes
        var lastMtime: TimeInterval = 0
        var seen: Set<String> = []
        guard let files = jsonlFiles(in: claudeDir, recursive: true) else { return nil }
        for metadata in files {
            let path = metadata.url.path
            seen.insert(path)
            lastMtime = max(lastMtime, metadata.mtime)

            if metadata.mtime < todayStart, claudeIndexes[path] == nil {
                claudeIndexes[path] = ClaudeFileIndex(
                    cursor: FileCursor(inode: metadata.inode, size: metadata.size,
                                       offset: metadata.size), mtime: metadata.mtime)
                continue
            }

            var index = claudeIndexes[path] ?? ClaudeFileIndex(
                cursor: FileCursor(inode: metadata.inode, size: metadata.size, offset: 0),
                mtime: metadata.mtime)
            let cursorWasReset = resetCursorIfNeeded(&index.cursor, metadata: metadata)
            if cursorWasReset {
                index.tokensToday = 0
                index.usageEpochs.removeAll(keepingCapacity: true)
            }
            if metadata.mtime >= todayStart {
                let readSucceeded = readAppended(
                    metadata,
                    cursor: &index.cursor,
                    bytesRead: &metrics.claudeBytesRead
                ) { data, range in
                    guard self.containsRawMarker(data, range: range,
                                                 marker: Self.claudeUsageMarker) else { return }
                    let lineData = data.subdata(in: range)
                    guard let obj = try? JSONSerialization.jsonObject(with: lineData) as? [String: Any],
                          let message = obj["message"] as? [String: Any],
                          let usage = message["usage"] as? [String: Any] else { return }
                    let epoch = self.parseISO(obj["timestamp"] as? String)
                    if let epoch, epoch < todayStart { return }
                    index.tokensToday += self.intVal(usage["input_tokens"])
                        + self.intVal(usage["output_tokens"])
                        + self.intVal(usage["cache_creation_input_tokens"])
                        + self.intVal(usage["cache_read_input_tokens"])
                    if let epoch { index.usageEpochs.append(epoch) }
                }
                guard readSucceeded else {
                    claudeIndexes = originalIndexes
                    return nil
                }
            } else {
                index.cursor.size = metadata.size
                if cursorWasReset { index.cursor.offset = metadata.size }
            }
            index.mtime = metadata.mtime
            index.usageEpochs.removeAll { $0 < todayStart || now - $0 >= 5 * 3600 }
            claudeIndexes[path] = index
        }
        claudeIndexes = claudeIndexes.filter { seen.contains($0.key) }

        var s = ClaudeStatus()
        s.tokensToday = claudeIndexes.values.reduce(0) { $0 + $1.tokensToday }
        if let first = claudeIndexes.values.flatMap(\.usageEpochs).min() {
            s.sessionMin = max(0, Int((now - first) / 60))
        }
        s.status = statusFromDelta(lastMtime > 0 ? now - lastMtime : 1e9)
        return s
    }

    // MARK: - Codex

    static func parsedCodexQuota(rateLimits: [String: Any], now: Double) -> CodexStatus {
        var windows: [CodexQuotaWindow] = []
        for (key, fallbackMinutes) in [
            ("primary", 5 * 60),
            ("secondary", 7 * 24 * 60),
        ] {
            guard let window = rateLimits[key] as? [String: Any] else { continue }
            windows.append(CodexQuotaWindow(
                usedPercent: (window["used_percent"] as? NSNumber)?.doubleValue,
                windowMinutes: (window["window_minutes"] as? NSNumber)?.intValue
                    ?? fallbackMinutes,
                resetsAt: (window["resets_at"] as? NSNumber)?.doubleValue
            ))
        }

        let classified = classifyCodexQuota(windows)
        var status = CodexStatus()
        if let window = classified.primary {
            status.primaryPct = window.usedPercent
            status.primaryWindowMin = window.windowMinutes
            status.primaryResetMin = window.resetsAt.map { max(0, Int(($0 - now) / 60)) }
        }
        if let window = classified.weekly {
            status.weeklyPct = window.usedPercent
            status.weeklyWindowMin = window.windowMinutes
            status.weeklyResetMin = window.resetsAt.map { max(0, Int(($0 - now) / 60)) }
        }
        return status
    }

    private func dayDirectory(for epoch: TimeInterval) -> URL {
        let components = Calendar.current.dateComponents([.year, .month, .day],
                                                         from: Date(timeIntervalSince1970: epoch))
        return codexDir
            .appendingPathComponent(String(format: "%04d", components.year ?? 0))
            .appendingPathComponent(String(format: "%02d", components.month ?? 0))
            .appendingPathComponent(String(format: "%02d", components.day ?? 0))
    }

    private func codexValues(in data: Data, range: Range<Int>)
        -> (total: Int?, rateLimits: [String: Any]?, timestamp: TimeInterval)? {
        guard containsRawMarker(data, range: range, marker: Self.codexTokenMarker),
              let obj = try? JSONSerialization.jsonObject(with: data.subdata(in: range)) as? [String: Any],
              let payload = obj["payload"] as? [String: Any],
              payload["type"] as? String == "token_count" else { return nil }
        let info = payload["info"] as? [String: Any]
        let totalUsage = info?["total_token_usage"] as? [String: Any]
        let total = (totalUsage?["total_tokens"] as? NSNumber)?.intValue
        return (total, payload["rate_limits"] as? [String: Any],
                parseISO(obj["timestamp"] as? String) ?? 0)
    }

    private func bootstrapCodex(_ metadata: FileMetadata,
                                metrics: inout RoundMetrics) -> CodexFileIndex? {
        var index = CodexFileIndex(
            cursor: FileCursor(inode: metadata.inode, size: metadata.size,
                               offset: metadata.size), mtime: metadata.mtime)
        guard metadata.size > 0 else { return index }
        guard let handle = try? openFileProvider(metadata.url) else { return nil }
        defer { try? handle.close() }
        var roundBytesRead = 0

        var position = metadata.size
        var rightFragment = Data()
        var needsEOFPartial = true
        var foundTotal = false
        var foundRateLimits = false

        while position > 0, !(foundTotal && foundRateLimits) {
            let count = Int(min(UInt64(Self.readChunkSize), position))
            let start = position - UInt64(count)
            do {
                try handle.seek(toOffset: start)
            } catch {
                return nil
            }
            let maybeBlock: Data?
            do {
                maybeBlock = try readChunkProvider(handle, count)
            } catch {
                return nil
            }
            guard let block = maybeBlock, block.count == count else { return nil }
            roundBytesRead += block.count
            var combined = block
            combined.append(rightFragment)

            if needsEOFPartial {
                if let newline = combined.lastIndex(of: 0x0a) {
                    index.cursor.partialLine = combined.subdata(in: (newline + 1)..<combined.count)
                    combined.removeSubrange((newline + 1)..<combined.count)
                    needsEOFPartial = false
                } else {
                    rightFragment = combined
                    position = start
                    if start == 0 {
                        index.cursor.partialLine = combined
                        needsEOFPartial = false
                    }
                    continue
                }
            }

            let parseStart: Int
            if start > 0 {
                guard let newline = combined.firstIndex(of: 0x0a) else {
                    rightFragment = combined
                    position = start
                    continue
                }
                rightFragment = combined.subdata(in: 0..<(newline + 1))
                parseStart = newline + 1
            } else {
                rightFragment.removeAll(keepingCapacity: true)
                parseStart = 0
            }

            var ranges: [Range<Int>] = []
            var lineStart = parseStart
            if parseStart < combined.count {
                for offset in parseStart..<combined.count where combined[offset] == 0x0a {
                    if offset > lineStart { ranges.append(lineStart..<offset) }
                    lineStart = offset + 1
                }
            }
            for range in ranges.reversed() {
                guard let values = codexValues(in: combined, range: range) else { continue }
                if let total = values.total {
                    index.totalTokens = max(index.totalTokens, total)
                    foundTotal = true
                }
                if let rateLimits = values.rateLimits, !foundRateLimits {
                    index.rateLimits = rateLimits
                    index.rateLimitsTs = values.timestamp
                    foundRateLimits = true
                }
                if foundTotal && foundRateLimits { break }
            }
            position = start
        }
        metrics.codexBytesRead += roundBytesRead
        return index
    }

    private func readCodex(now: TimeInterval, metrics: inout RoundMetrics) -> CodexStatus? {
        let originalIndexes = codexIndexes
        var lastMtime: TimeInterval = 0
        let currentDir = dayDirectory(for: now)
        let previousDate = Calendar.current.date(byAdding: .day, value: -1,
                                                 to: Date(timeIntervalSince1970: now))!
        let previousDir = dayDirectory(for: previousDate.timeIntervalSince1970)
        guard let currentFiles = jsonlFiles(in: currentDir, recursive: false),
              let previousFiles = jsonlFiles(in: previousDir, recursive: false) else { return nil }
        for metadata in currentFiles + previousFiles { lastMtime = max(lastMtime, metadata.mtime) }

        var seen: Set<String> = []
        for metadata in currentFiles {
            let path = metadata.url.path
            seen.insert(path)
            var index: CodexFileIndex
            if var existing = codexIndexes[path] {
                if resetCursorIfNeeded(&existing.cursor, metadata: metadata) {
                    guard let rebuilt = bootstrapCodex(metadata, metrics: &metrics) else {
                        codexIndexes = originalIndexes
                        return nil
                    }
                    index = rebuilt
                } else {
                    let readSucceeded = readAppended(
                        metadata,
                        cursor: &existing.cursor,
                        bytesRead: &metrics.codexBytesRead
                    ) { data, range in
                        guard let values = self.codexValues(in: data, range: range) else { return }
                        if let total = values.total { existing.totalTokens = max(existing.totalTokens, total) }
                        if let rateLimits = values.rateLimits,
                           values.timestamp >= existing.rateLimitsTs {
                            existing.rateLimits = rateLimits
                            existing.rateLimitsTs = values.timestamp
                        }
                    }
                    guard readSucceeded else {
                        codexIndexes = originalIndexes
                        return nil
                    }
                    existing.mtime = metadata.mtime
                    index = existing
                }
            } else {
                guard let bootstrapped = bootstrapCodex(metadata, metrics: &metrics) else {
                    codexIndexes = originalIndexes
                    return nil
                }
                index = bootstrapped
            }
            codexIndexes[path] = index
        }
        codexIndexes = codexIndexes.filter { seen.contains($0.key) }

        var s = CodexStatus()
        s.tokensToday = codexIndexes.values.reduce(0) { $0 + $1.totalTokens }
        s.status = statusFromDelta(lastMtime > 0 ? now - lastMtime : 1e9)
        if let latest = codexIndexes.values.filter({ $0.rateLimits != nil })
            .max(by: { $0.rateLimitsTs < $1.rateLimitsTs }), let rl = latest.rateLimits {
            let quota = Self.parsedCodexQuota(rateLimits: rl, now: now)
            s.primaryPct = quota.primaryPct
            s.primaryWindowMin = quota.primaryWindowMin
            s.primaryResetMin = quota.primaryResetMin
            s.weeklyPct = quota.weeklyPct
            s.weeklyWindowMin = quota.weeklyWindowMin
            s.weeklyResetMin = quota.weeklyResetMin
        }
        return s
    }
}

extension Snapshot {
    /// Serializes to the exact JSON shape the firmware's parseStatusJson expects.
    func jsonData() -> Data {
        func num(_ v: Int?) -> Any { v.map { $0 as Any } ?? NSNull() }
        func num(_ v: Double?) -> Any { v.map { $0 as Any } ?? NSNull() }
        let dict: [String: Any] = [
            "ts": ts,
            "music_playing": musicPlaying,
            "claude": [
                "status": claude.status,
                "tokens_today": claude.tokensToday,
                "session_min": claude.sessionMin,
                "session_window_min": claude.sessionWindowMin,
                "five_hour_pct": num(claude.fiveHourPct),
                "five_hour_reset_min": num(claude.fiveHourResetMin),
                "seven_day_pct": num(claude.sevenDayPct),
                "seven_day_reset_min": num(claude.sevenDayResetMin),
                "needs_input": claude.needsInput,
            ],
            "codex": [
                "status": codex.status,
                "tokens_today": codex.tokensToday,
                "primary_pct": num(codex.primaryPct),
                "primary_window_min": num(codex.primaryWindowMin),
                "primary_reset_min": num(codex.primaryResetMin),
                "weekly_pct": num(codex.weeklyPct),
                "weekly_window_min": num(codex.weeklyWindowMin),
                "weekly_reset_min": num(codex.weeklyResetMin),
                "needs_input": codex.needsInput,
            ],
        ]
        return (try? JSONSerialization.data(withJSONObject: dict)) ?? Data("{}".utf8)
    }
}
