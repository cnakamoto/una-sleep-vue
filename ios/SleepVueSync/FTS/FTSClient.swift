//
//  FTSClient.swift
//  SleepVueSync
//
//  CoreBluetooth client for the UNA Watch BLE File Transfer Service.
//  One operation is in flight at a time; all operations are async/await.
//
//  Pairing: the watch requires a bonded, encrypted link (FTS spec §Security).
//  iOS triggers the system pairing sheet automatically on the first access to
//  an encrypted characteristic — no code needed, just surface the error if the
//  user cancels.
//
//  Read model (protocol v5): a single READ with a 4096-byte window; the watch
//  answers each READ_PACING with a burst of READ_DATA notifications. Chunks
//  are self-describing — we reassemble strictly by chunkOffset and pace from
//  the contiguous end, so a dropped notification is re-requested, never
//  skipped. Against a v4 watch the same code degrades to stop-and-wait.
//

import CoreBluetooth
import Foundation

@MainActor
final class FTSClient: NSObject, ObservableObject {

    enum Phase: Equatable {
        case idle
        case starting
        case scanning
        case connecting
        case handshaking
        case ready
        case failed(String)
    }

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var deviceName: String?
    @Published private(set) var protocolVersion: UInt32 = 0
    /// Timestamped BLE event log for on-device debugging (newest at the end).
    @Published private(set) var debugLog: [String] = []

    // MARK: - Private state

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rawChar: CBCharacteristic?
    private var versionChar: CBCharacteristic?

    // Connect pipeline continuations.
    private var discoveryCont: CheckedContinuation<CBPeripheral, Error>?
    private var connectCont: CheckedContinuation<Void, Error>?
    private var scanFallbackTask: Task<Void, Never>?
    private var scanTimeoutTask: Task<Void, Never>?
    private var connectTimeoutTask: Task<Void, Never>?

    // In-flight FTS operation (at most one).
    private enum OpKind { case listDir, read, digest }
    private var opKind: OpKind?
    private var listCont: CheckedContinuation<[FTSEntry], Error>?
    private var readCont: CheckedContinuation<Data, Error>?
    private var digestCont: CheckedContinuation<FTSDigest, Error>?

    // LISTDIR / READ accumulators.
    private var entries: [FTSEntry] = []
    private var chunks: [UInt32: Data] = [:]
    private var contiguousEnd: UInt32 = 0
    private var totalLength: UInt32 = 0
    private var readProgress: ((Double) -> Void)?

    private var watchdog: Task<Void, Never>?
    private static let watchdogSeconds: UInt64 = 15

    private static let serviceUUID = CBUUID(string: FTS.serviceUUIDString)
    private static let versionUUID = CBUUID(string: FTS.versionUUIDString)
    private static let rawUUID = CBUUID(string: FTS.rawTransferUUIDString)

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Public API

    /// Scan → connect → discover FTS → enable notifications → read protocol
    /// version. Resolves when the client is `.ready`. Times out after
    /// 20 s of scanning with no match, or 30 s of stalled connect/handshake.
    func connect() async throws {
        guard connectCont == nil, discoveryCont == nil else { throw FTSError.busy }
        if phase == .ready { return }
        phase = .starting

        let found: CBPeripheral = try await withCheckedThrowingContinuation { cont in
            discoveryCont = cont
            startScanIfPowered(filtered: true)
            // Fallback: some watches may not put 0xFEBB in the advertisement.
            // After 6 s with no hit, rescan unfiltered and match the name.
            scanFallbackTask = Task { [weak self] in
                try? await Task.sleep(nanoseconds: 6_000_000_000)
                guard let self, !Task.isCancelled, self.discoveryCont != nil else { return }
                self.log("no FTS advertisement in 6 s — rescanning unfiltered, matching name")
                self.central.stopScan()
                self.startScanIfPowered(filtered: false)
            }
            scanTimeoutTask = Task { [weak self] in
                try? await Task.sleep(nanoseconds: 20_000_000_000)
                guard let self, !Task.isCancelled, self.discoveryCont != nil else { return }
                self.central.stopScan()
                let c = self.discoveryCont
                self.discoveryCont = nil
                self.phase = .failed("No watch found")
                c?.resume(throwing: FTSError.scanTimeout)
            }
        }

        scanFallbackTask?.cancel()
        scanTimeoutTask?.cancel()
        central.stopScan()
        peripheral = found
        deviceName = found.name
        phase = .connecting
        log("connecting to \(found.name ?? "device")…")
        central.connect(found, options: nil)

        connectTimeoutTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 30_000_000_000)
            guard let self, !Task.isCancelled, self.connectCont != nil else { return }
            if let p = self.peripheral { self.central.cancelPeripheralConnection(p) }
            self.failConnect(FTSError.connectTimeout)
        }
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            connectCont = cont
        }
        connectTimeoutTask?.cancel()
    }

    /// Abort an in-progress scan/connect and go back to idle.
    func cancelConnect() {
        scanFallbackTask?.cancel()
        scanTimeoutTask?.cancel()
        connectTimeoutTask?.cancel()
        central.stopScan()
        let discovery = discoveryCont
        discoveryCont = nil
        discovery?.resume(throwing: CancellationError())
        let connect = connectCont
        connectCont = nil
        connect?.resume(throwing: CancellationError())
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        failPending(CancellationError())
        settleDisconnected()
        log("connect cancelled")
    }

    func disconnect() {
        scanFallbackTask?.cancel()
        scanTimeoutTask?.cancel()
        connectTimeoutTask?.cancel()
        failPending(FTSError.disconnected)
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        settleDisconnected()
    }

    /// LISTDIR an absolute path (e.g. "/Apps/SleepVue/").
    func listDir(_ path: String) async throws -> [FTSEntry] {
        try beginOp(.listDir)
        entries = []
        return try await withCheckedThrowingContinuation { cont in
            listCont = cont
            writeRaw(FTSPacket.makeListDir(path: path))
        }
    }

    /// Read a whole file. Windowed (v5) with automatic degrade to classic
    /// stop-and-wait on a v4 watch. Progress is 0...1 by contiguous bytes.
    func readFile(_ path: String, progress: ((Double) -> Void)? = nil) async throws -> Data {
        try beginOp(.read)
        chunks = [:]
        contiguousEnd = 0
        totalLength = 0
        readProgress = progress
        return try await withCheckedThrowingContinuation { cont in
            readCont = cont
            writeRaw(FTSPacket.makeRead(path: path, offset: 0, size: FTS.readWindow))
        }
    }

    /// DIGEST (protocol v5+). Gate on `protocolVersion >= 5`.
    func digest(_ path: String) async throws -> FTSDigest {
        try beginOp(.digest)
        return try await withCheckedThrowingContinuation { cont in
            digestCont = cont
            writeRaw(FTSPacket.makeDigest(path: path))
        }
    }

    // MARK: - Operation plumbing

    private func beginOp(_ kind: OpKind) throws {
        guard phase == .ready, peripheral != nil, rawChar != nil else { throw FTSError.notReady }
        guard opKind == nil else { throw FTSError.busy }
        opKind = kind
        armWatchdog()
    }

    private func writeRaw(_ data: Data) {
        guard let peripheral, let rawChar else { failPending(FTSError.notReady); return }
        // WRITE without response is the only write property FTS exposes. Our
        // commands are tiny and infrequent (one per 4 KB window), so the
        // iOS write queue absorbing them is a non-issue in practice.
        peripheral.writeValue(data, for: rawChar, type: .withoutResponse)
        armWatchdog()
    }

    private func armWatchdog() {
        watchdog?.cancel()
        watchdog = Task { [weak self] in
            try? await Task.sleep(nanoseconds: Self.watchdogSeconds * 1_000_000_000)
            guard let self, !Task.isCancelled else { return }
            self.failPending(FTSError.timeout)
        }
    }

    private func finishOp() {
        watchdog?.cancel()
        watchdog = nil
        opKind = nil
        listCont = nil
        readCont = nil
        digestCont = nil
        readProgress = nil
    }

    private func failPending(_ error: Error) {
        let list = listCont, read = readCont, digest = digestCont
        finishOp()
        list?.resume(throwing: error)
        read?.resume(throwing: error)
        digest?.resume(throwing: error)
    }

    // MARK: - Debug log

    private func log(_ message: String) {
        let stamp = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
        debugLog.append("\(stamp)  \(message)")
        if debugLog.count > 100 { debugLog.removeFirst(debugLog.count - 100) }
    }

    // MARK: - Scanning

    private func startScanIfPowered(filtered: Bool) {
        guard central.state == .poweredOn else {
            log("scan deferred — central state \(Self.stateName(central.state))")
            return // delegate re-fires on power-on
        }
        phase = .scanning
        log(filtered ? "scanning (FTS service filter)" : "scanning (unfiltered)")
        central.scanForPeripherals(
            withServices: filtered ? [Self.serviceUUID] : nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    private static func stateName(_ state: CBManagerState) -> String {
        switch state {
        case .unknown: return "unknown"
        case .resetting: return "resetting"
        case .unsupported: return "unsupported"
        case .unauthorized: return "unauthorized"
        case .poweredOff: return "poweredOff"
        case .poweredOn: return "poweredOn"
        @unknown default: return "?\(state.rawValue)"
        }
    }

    // MARK: - Central/peripheral event handling (hopped to MainActor)

    private func handleCentralState(_ state: CBManagerState) {
        log("central state: \(Self.stateName(state))")
        switch state {
        case .poweredOn:
            if discoveryCont != nil { startScanIfPowered(filtered: true) }
        case .poweredOff:
            failAll(FTSError.bluetoothUnavailable("Bluetooth is off"))
        case .unauthorized:
            failAll(FTSError.bluetoothUnavailable("Bluetooth access denied — allow it in Settings"))
        case .unsupported:
            failAll(FTSError.bluetoothUnavailable("BLE not supported on this device"))
        default:
            break
        }
    }

    private func handleDiscover(_ p: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        guard discoveryCont != nil else { return }
        let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        let name = p.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let serviceList = uuids.isEmpty ? "—" : uuids.map(\.uuidString).joined(separator: ",")
        log("heard \(name ?? "unnamed") rssi=\(rssi) services=\(serviceList)")
        let isWatch = uuids.contains(Self.serviceUUID)
            || (name?.localizedCaseInsensitiveContains("una") ?? false)
        guard isWatch else { return }
        log("matched as UNA Watch")
        let cont = discoveryCont
        discoveryCont = nil
        cont?.resume(returning: p)
    }

    private func handleConnect() {
        guard let peripheral else { return }
        phase = .handshaking
        log("link up — discovering FTS service")
        peripheral.delegate = self
        peripheral.discoverServices([Self.serviceUUID])
    }

    private func handleServicesDiscovered(_ p: CBPeripheral, _ error: Error?) {
        if let error { failConnect(error); return }
        guard let service = p.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            failConnect(FTSError.serviceNotFound)
            return
        }
        log("FTS service found")
        p.discoverCharacteristics([Self.versionUUID, Self.rawUUID], for: service)
    }

    private func handleCharacteristicsDiscovered(_ p: CBPeripheral, _ error: Error?) {
        if let error { failConnect(error); return }
        guard let service = p.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            failConnect(FTSError.serviceNotFound)
            return
        }
        rawChar = service.characteristics?.first(where: { $0.uuid == Self.rawUUID })
        versionChar = service.characteristics?.first(where: { $0.uuid == Self.versionUUID })
        guard let rawChar else { failConnect(FTSError.serviceNotFound); return }
        // FTS requires: enable notifications before issuing commands.
        log("characteristics ok — enabling notifications")
        p.setNotifyValue(true, for: rawChar)
    }

    private func handleNotificationState(_ characteristic: CBCharacteristic, _ error: Error?) {
        if let error { failConnect(error); return }
        guard characteristic.uuid == Self.rawUUID, characteristic.isNotifying else { return }
        guard let versionChar, let peripheral else { failConnect(FTSError.serviceNotFound); return }
        log("notifications enabled — reading protocol version")
        peripheral.readValue(for: versionChar)
    }

    private func handleValue(_ characteristic: CBCharacteristic, _ error: Error?) {
        if characteristic.uuid == Self.versionUUID {
            if let error { failConnect(error); return }
            guard let data = characteristic.value, data.count >= 4 else {
                failConnect(FTSError.malformedResponse)
                return
            }
            protocolVersion = data.leU32(at: 0)
            phase = .ready
            log("ready — FTS protocol v\(protocolVersion)")
            let cont = connectCont
            connectCont = nil
            cont?.resume()
            return
        }
        guard characteristic.uuid == Self.rawUUID, let data = characteristic.value else { return }
        armWatchdog() // traffic alive
        routeRawResponse(data)
    }

    private func routeRawResponse(_ data: Data) {
        guard let cmd = data.first else { return }
        switch (opKind, FTS.Command(rawValue: cmd)) {
        case (.read, .readData): handleReadData(data)
        case (.listDir, .listDirEntry): handleListDirEntry(data)
        case (.digest, .digestStatus): handleDigestStatus(data)
        default: break // stale/duplicate notification — ignore
        }
    }

    // MARK: - READ state machine

    private func handleReadData(_ data: Data) {
        guard let chunk = FTSPacket.parseReadData(data) else { failPending(FTSError.malformedResponse); return }
        guard chunk.status == .ok else {
            log("READ rejected (status \(chunk.status.rawValue))")
            failPending(FTSError.watchRejected(chunk.status))
            return
        }

        if totalLength == 0 { totalLength = chunk.totalLength }
        if chunk.totalLength == 0 { // empty file
            let cont = readCont
            finishOp()
            cont?.resume(returning: Data())
            return
        }

        // Reassemble by chunkOffset. Clip retransmits of bytes already consumed.
        var offset = chunk.chunkOffset
        var payload = chunk.data
        if offset + UInt32(payload.count) > chunk.totalLength { // defensive bound
            payload = payload.prefix(Int(chunk.totalLength - offset))
        }
        if offset < contiguousEnd {
            let overlap = Int(contiguousEnd - offset)
            if overlap < payload.count {
                payload = payload.dropFirst(overlap)
                offset = contiguousEnd
            }
        }
        if offset >= contiguousEnd, !payload.isEmpty {
            if let existing = chunks[offset], existing.count >= payload.count {
                // keep the longer copy
            } else {
                chunks[offset] = payload
            }
        }
        while let next = chunks[contiguousEnd] {
            contiguousEnd += UInt32(next.count)
        }
        readProgress?(chunk.totalLength == 0 ? 1 : min(1, Double(contiguousEnd) / Double(chunk.totalLength)))

        if contiguousEnd >= chunk.totalLength {
            var assembled = Data()
            assembled.reserveCapacity(Int(chunk.totalLength))
            var cursor: UInt32 = 0
            while cursor < chunk.totalLength, let piece = chunks[cursor] {
                assembled.append(piece)
                cursor += UInt32(piece.count)
            }
            guard assembled.count == Int(chunk.totalLength) else {
                failPending(FTSError.malformedResponse)
                return
            }
            log("READ ok — \(assembled.count) bytes")
            let cont = readCont
            finishOp()
            cont?.resume(returning: assembled)
        } else {
            // Pace from the contiguous end: re-request exactly what we don't have.
            writeRaw(FTSPacket.makeReadPacing(offset: contiguousEnd, size: FTS.readWindow))
        }
    }

    // MARK: - LISTDIR / DIGEST handlers

    private func handleListDirEntry(_ data: Data) {
        guard let item = FTSPacket.parseListDirEntry(data) else { failPending(FTSError.malformedResponse); return }
        switch item {
        case .entry(let entry): entries.append(entry)
        case .done:
            let result = entries
            log("LISTDIR ok — \(result.count) entries")
            let cont = listCont
            finishOp()
            cont?.resume(returning: result)
        }
    }

    private func handleDigestStatus(_ data: Data) {
        guard let digest = FTSPacket.parseDigestStatus(data) else { failPending(FTSError.malformedResponse); return }
        guard digest.status == .ok else {
            log("DIGEST rejected (status \(digest.status.rawValue))")
            failPending(FTSError.watchRejected(digest.status))
            return
        }
        log("DIGEST ok — \(digest.fileSize) bytes")
        let cont = digestCont
        finishOp()
        cont?.resume(returning: digest)
    }

    // MARK: - Failure / disconnect

    private func failConnect(_ error: Error) {
        log("connect failed: \(error.localizedDescription)")
        let cont = connectCont
        connectCont = nil
        phase = .failed(error.localizedDescription)
        cont?.resume(throwing: error)
    }

    private func failAll(_ error: Error) {
        let discovery = discoveryCont
        discoveryCont = nil
        discovery?.resume(throwing: error)
        failConnect(error)
        failPending(error)
    }

    private func settleDisconnected() {
        rawChar = nil
        versionChar = nil
        peripheral = nil
        protocolVersion = 0
        if phase != .idle { phase = .idle }
    }
}

// MARK: - CoreBluetooth delegate shims (delegates fire on the main queue)

extension FTSClient: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        let state = central.state
        Task { @MainActor in self.handleCentralState(state) }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        Task { @MainActor in self.handleDiscover(peripheral, advertisementData: advertisementData, rssi: RSSI) }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in self.handleConnect() }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in self.failConnect(error ?? FTSError.disconnected) }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            self.log("disconnected\(error.map { ": \($0.localizedDescription)" } ?? "")")
            self.failPending(FTSError.disconnected)
            self.settleDisconnected()
        }
    }
}

extension FTSClient: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in self.handleServicesDiscovered(peripheral, error) }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in self.handleCharacteristicsDiscovered(peripheral, error) }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in self.handleNotificationState(characteristic, error) }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in self.handleValue(characteristic, error) }
    }
}
