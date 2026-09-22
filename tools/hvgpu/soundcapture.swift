// hvgpu: --record-sound PATH — this app's audio, written to a WAV file.
//
// The machine's virtio-snd output (VZHostAudioOutputStreamSink) and the MIDI
// synth (an AVAudioEngine of our own) both play from this process, and what
// they play is only ever heard, never seen: a test that "plays" can succeed at
// every API and still be silent on the Mac. ScreenCaptureKit can tap exactly
// this app's audio (it offers the stream, the volume of the speakers does not
// matter, and nothing else the Mac plays is caught), so the test scripts can
// record a run and look at when sound actually happened.
//
// The WAV is written by hand (44-byte header, PCM16): AVAudioFile has no
// explicit close, and a file whose header is only put right when the object is
// deallocated is a file a test that ends in exit(0) cannot trust. Here the
// data is written as it comes and the header is fixed at finalize.
import AppKit
import AVFoundation
import CoreMedia
import ScreenCaptureKit

final class SoundCapture: NSObject, SCStreamDelegate, SCStreamOutput {
    static let flag = "--record-sound"

    private let url: URL
    private let queue = DispatchQueue(label: "hvgpu.soundcapture")
    private var handle: FileHandle?
    private var frames = 0
    private var stream: SCStream?
    private(set) var started = false
    private(set) var finished = false

    /// What the capture wrote, for the log. Zero frames with the stream running
    /// is itself the finding: the app produced no audio at all.
    var summary: String {
        let seconds = Double(frames) / 48000
        return "record-sound: \(frames) frames (\(String(format: "%.1f", seconds)) s) -> \(url.path)"
    }

    static func startIfRequested() -> SoundCapture? {
        guard let path = option(flag) else { return nil }
        let capture = SoundCapture(url: URL(fileURLWithPath: path))
        capture.start()
        return capture
    }

    private init(url: URL) {
        self.url = url
        super.init()
    }

    private func start() {
        FileManager.default.createFile(atPath: url.path, contents: nil)
        guard let handle = FileHandle(forWritingAtPath: url.path) else {
            log("record-sound: cannot open \(url.path)")
            return
        }
        self.handle = handle
        handle.write(Data(WavWriter.header(frames: 0, bytes: 0)))

        Task { [self] in
            do {
                let content = try await SCShareableContent.current
                guard let display = content.displays.first else {
                    log("record-sound: no display to capture audio from")
                    return
                }
                // the whole display's audio. Per-application exclusion exists,
                // but a test Mac plays nothing else, and whole-display capture
                // does not depend on how this app appears to ScreenCaptureKit
                // (a bundle binary exec'd by a script, say).
                let filter = SCContentFilter(display: display, excludingWindows: [])

                let config = SCStreamConfiguration()
                config.capturesAudio = true
                // the default excludes the capturing process itself, and this
                // process is the one there is to hear
                config.excludesCurrentProcessAudio = false
                config.sampleRate = 48000
                config.channelCount = 2

                let stream = SCStream(filter: filter, configuration: config, delegate: self)
                try stream.addStreamOutput(self, type: .audio, sampleHandlerQueue: queue)
                try await stream.startCapture()
                self.stream = stream
                started = true
                log("record-sound: capturing this app's audio to \(url.path)")
            } catch {
                log("record-sound: capture did not start: \(error.localizedDescription)")
                log("record-sound: Screen Recording permission may be needed (System Settings ▸ Privacy & Security)")
            }
        }
    }

    /// Closes the WAV (not `finalize`: NSObject owns that name). Safe to
    /// call more than once; the header is rewritten so
    /// the file is valid whatever happened since.
    func finish() {
        let wasFinished: Bool = queue.sync {
            if finished { return true }
            finished = true
            if let stream {
                Task { try? await stream.stopCapture() }
            }
            if let handle {
                // the header at the front again, with the real sizes
                _ = try? handle.seek(toOffset: 0)
                handle.write(Data(WavWriter.header(frames: frames, bytes: frames * 4)))
                try? handle.close()
            }
            return false
        }
        if !wasFinished && started {
            log(summary)
        }
    }

    // MARK: SCStreamOutputDelegate

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
                of type: SCStreamOutputType) {
        guard type == .audio, let handle else { return }
        guard CMSampleBufferDataIsReady(sampleBuffer) else { return }

        // CMSampleBuffer -> AVAudioPCMBuffer -> interleaved PCM16
        var blockBuffer: CMBlockBuffer?
        var needed = 0
        CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer,
            bufferListSizeNeededOut: &needed, bufferListOut: nil, bufferListSize: 0,
            blockBufferAllocator: nil, blockBufferMemoryAllocator: nil,
            flags: kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
            blockBufferOut: nil)
        guard needed >= MemoryLayout<AudioBufferList>.size else { return }

        // an AudioBufferList carries its buffers inline: the struct itself has
        // room for one, and stereo needs two, so the list is allocated at the
        // size the sample buffer asked for -- never on the stack
        let listPointer = UnsafeMutablePointer<UInt8>.allocate(capacity: needed)
        defer { listPointer.deallocate() }
        let status = listPointer.withMemoryRebound(to: AudioBufferList.self, capacity: 1) {
            CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer,
                bufferListSizeNeededOut: &needed, bufferListOut: $0,
                bufferListSize: needed, blockBufferAllocator: nil,
                blockBufferMemoryAllocator: nil,
                flags: kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
                blockBufferOut: &blockBuffer)
        }
        guard status == noErr else { return }

        let buffers = UnsafeMutableAudioBufferListPointer(
            listPointer.withMemoryRebound(to: AudioBufferList.self, capacity: 1) { $0 })
        guard buffers.count == 2, let left = buffers[0].mData, let right = buffers[1].mData else {
            // not the stereo pair asked for: ignored, and said so once
            if frames == 0 { log("record-sound: unexpected audio layout (\(buffers.count) buffers)") }
            return
        }
        let count = Int(buffers[0].mDataByteSize) / MemoryLayout<Float>.size

        let l = left.assumingMemoryBound(to: Float.self)
        let r = right.assumingMemoryBound(to: Float.self)
        var interleaved = [Int16](repeating: 0, count: count * 2)
        for i in 0..<count {
            interleaved[i * 2] = Int16(max(-32768, min(32767, l[i] * 32767)))
            interleaved[i * 2 + 1] = Int16(max(-32768, min(32767, r[i] * 32767)))
        }
        handle.write(interleaved.withUnsafeBytes { Data($0) })
        frames += count
    }
}

/// A WAV's first 44 bytes: PCM16 stereo 48 kHz.
enum WavWriter {
    static func header(frames: Int, bytes: Int) -> [UInt8] {
        var h = [UInt8]()
        func le32(_ v: UInt32) { h.append(contentsOf: withUnsafeBytes(of: v.littleEndian) { Array($0) }) }
        func le16(_ v: UInt16) { h.append(contentsOf: withUnsafeBytes(of: v.littleEndian) { Array($0) }) }
        h.append(contentsOf: Array("RIFF".utf8))
        le32(UInt32(36 + bytes))
        h.append(contentsOf: Array("WAVE".utf8))
        h.append(contentsOf: Array("fmt ".utf8))
        le32(16)                              // the fmt chunk's size
        le16(1)                               // PCM
        le16(2)                               // stereo
        le32(48000)
        le32(48000 * 4)                       // bytes per second
        le16(4)                               // block align
        le16(16)                              // bits per sample
        h.append(contentsOf: Array("data".utf8))
        le32(UInt32(bytes))
        return h
    }
}
