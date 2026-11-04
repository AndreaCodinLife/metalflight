import Foundation

/// Watches only the active game's log and reports an explicit Metal out-of-memory error.
@MainActor
enum GameMemoryErrorMonitor {
    static func watch(log: URL, process: Process, onMemoryError: @escaping @MainActor () -> Void) -> Task<Void, Never> {
        Task {
            guard let handle = try? FileHandle(forReadingFrom: log) else { return }
            defer { try? handle.close() }

            var offset: UInt64 = 0
            var partialLine = ""

            while !Task.isCancelled {
                do {
                    let attributes = try FileManager.default.attributesOfItem(atPath: log.path)
                    let fileSize = (attributes[.size] as? NSNumber)?.uint64Value ?? offset
                    if fileSize > offset {
                        try handle.seek(toOffset: offset)
                        let count = Int(min(fileSize - offset, 64 * 1024))
                        let data = try handle.read(upToCount: count) ?? Data()
                        offset += UInt64(data.count)

                        partialLine += String(decoding: data, as: UTF8.self)
                        let lines = partialLine.components(separatedBy: .newlines)
                        partialLine = lines.last ?? ""
                        if lines.dropLast().contains(where: isOutOfMemoryRecord) {
                            onMemoryError()
                            return
                        }
                    }
                } catch {
                    return
                }

                if !process.isRunning {
                    if isOutOfMemoryRecord(partialLine) { onMemoryError() }
                    return
                }

                try? await Task.sleep(nanoseconds: 300_000_000)
            }
        }
    }

    private static func isOutOfMemoryRecord(_ line: String) -> Bool {
        let lowercased = line.lowercased()
        guard lowercased.contains("mtlcommandbuffererrordomain"),
              lowercased.range(of: #"\bcode\s*=\s*8\b"#, options: .regularExpression) != nil else {
            return false
        }
        return lowercased.contains("insufficient memory") ||
            lowercased.contains("kiogpucommandbuffercallbackerroroutofmemory")
    }
}
