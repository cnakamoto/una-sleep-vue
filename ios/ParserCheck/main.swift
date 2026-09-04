//
//  ParserCheck — host-side verification harness (not part of the iOS app).
//
//  Compiles NightFile.swift + FTSProtocol.swift with swiftc and checks them
//  against real night files pulled from the watch:
//
//    swiftc -O -o parsercheck main.swift \
//        ../SleepVueSync/Model/NightFile.swift \
//        ../SleepVueSync/FTS/FTSProtocol.swift
//    ./parsercheck ../../nights/slp_20260903.bin
//
//  Output is line-for-line comparable with the Python reference parser
//  (tools/verify_night.py), and crc32 with python3 -c 'zlib.crc32(...)'.

import Foundation

func fmtTime(_ d: Date) -> String {
    let f = DateFormatter()
    f.dateFormat = "HH:mm"
    return f.string(from: d)
}

guard CommandLine.arguments.count > 1 else {
    print("usage: parsercheck <night.bin> [...]")
    exit(2)
}

for path in CommandLine.arguments.dropFirst() {
    let url = URL(fileURLWithPath: path)
    do {
        let data = try Data(contentsOf: url)
        let night = try NightFile.parse(data)
        let h = night.header

        print("file=\(url.lastPathComponent)")
        print("dateKey=\(h.dateKey) bed=\(fmtTime(night.bed)) wake=\(fmtTime(night.wake))")
        print("header: epochs=\(h.epochCount) total=\(h.totalMin) awake=\(h.awakeMin) "
            + "light=\(h.lightMin) deep=\(h.deepMin) hr=\(h.hrMin)/\(h.hrAvg)/\(h.hrMax) "
            + String(format: "flags=0x%02X", h.flags.rawValue))
        print("derived: total=\(night.totalMinutes) awake=\(night.minutes(of: .awake)) "
            + "light=\(night.minutes(of: .light)) deep=\(night.minutes(of: .deep)) "
            + "hr=\(night.hrMin)/\(night.hrAvg)/\(night.hrMax)")
        print(String(format: "crc32=0x%08X", CRC32.compute(data)))

        // Exercise the FTS packet codec round-trips while we're here.
        assert(FTSPacket.parseReadData(
            Data([0x11, 0x01, 0, 0, 5, 0, 0, 0, 9, 0, 0, 0, 3, 0, 0, 0, 0xAA, 0xBB, 0xCC])
        ).map { $0.chunkOffset == 5 && $0.totalLength == 9 && $0.data == Data([0xAA, 0xBB, 0xCC]) } == true)
        assert(FTSPacket.parseDigestStatus(
            Data([0x71, 0x01, 0, 0, 0x2C, 0x01, 0, 0, 0xDE, 0xAD, 0xBE, 0xEF])
        ).map { $0.fileSize == 300 && $0.crc32 == 0xEFBEADDE } == true)
    } catch {
        print("file=\(url.lastPathComponent) ERROR \(error)")
    }
}
