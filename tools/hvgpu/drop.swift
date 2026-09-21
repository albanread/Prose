// hvgpu: dropping files on the window puts them in the shared folder.
//
// The one gesture a person expects from a virtual machine — drag a file onto
// its window, find it inside — used to do nothing at all: nothing was
// registered for drags. Now the content view takes file drags and copies them
// into the folder the machine mounts as HostFS (hostfs.swift), which the
// guest's poller (patch 0081) makes appear on its own. A copy is never a move:
// the file stays where it was, and a second copy lands beside the first under
// a Finder-style "copy" name.
import AppKit
import Foundation

/// The URLs of the files a drag carries, or an empty list when it carries none.
private func draggedFileURLs(_ draggingInfo: NSDraggingInfo) -> [URL] {
    var urls: [URL] = []
    draggingInfo.enumerateDraggingItems(options: [], for: nil, classes: [NSURL.self],
        searchOptions: [.urlReadingFileURLsOnly: true]) { item, _, _ in
        if let url = item.item as? URL { urls.append(url) }
    }
    return urls
}

/// Where a drop goes: the folder the machine has (or, stopped, would have)
/// mounted, and whether it takes writes.
private func dropDestination() -> (url: URL, readOnly: Bool)? {
    guard let share = (appliedShares ?? hostShares()).first else { return nil }
    return (share.url, share.readOnly)
}

/// Copies files into the shared folder, on a background queue, and reports one
/// line for the status bar and the log. This is the whole of what a drop does,
/// kept on its own so anything can use it.
func copyFilesIntoShare(_ urls: [URL], report: @escaping (String) -> Void) {
    guard !urls.isEmpty else { return }
    guard let destination = dropDestination() else {
        report("No folder is shared — choose one in Settings")
        return
    }
    if destination.readOnly {
        report("The shared folder is read-only")
        return
    }

    DispatchQueue.global(qos: .userInitiated).async {
        let fm = FileManager.default
        try? fm.createDirectory(at: destination.url, withIntermediateDirectories: true)

        var copied = 0
        var failed: [String] = []
        for url in urls {
            // a name that is not taken: "Report.pdf", "Report copy.pdf",
            // "Report copy 2.pdf", ... — like the Finder, and unlike the
            // guest's Tracker, which asks first
            let base = url.deletingPathExtension()
            let name = url.lastPathComponent
            let ext = url.pathExtension.isEmpty ? "" : "." + url.pathExtension
            var target = destination.url.appendingPathComponent(name)
            var ordinal = 1
            while fm.fileExists(atPath: target.path) {
                ordinal += 1
                target = destination.url.appendingPathComponent(
                    "\(base.lastPathComponent) copy\(ordinal > 2 ? " \(ordinal - 1)" : "")\(ext)")
            }
            do {
                try fm.copyItem(at: url, to: target)
                copied += 1
            } catch {
                failed.append("\(name): \((error as NSError).localizedDescription)")
            }
        }

        DispatchQueue.main.async {
            var line = copied == 1 ? "Copied 1 item to HostFS"
                : "Copied \(copied) items to HostFS"
            if !failed.isEmpty {
                line = "Copied \(copied) of \(urls.count): \(failed.first ?? "")"
                log("drop: \(failed.joined(separator: "; "))")
            }
            report(line)
        }
    }
}

/// Registered on the content view (WindowChrome): answers for the drag, and
/// the drop itself is a copy into the share.
final class DropReceiver: NSObject, NSDraggingDestination {
    var flash: (() -> Void)?
    var report: (String) -> Void

    init(report: @escaping (String) -> Void) {
        self.report = report
    }

    func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        draggedFileURLs(sender).isEmpty ? [] : .copy
    }

    func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        draggedFileURLs(sender).isEmpty ? [] : .copy
    }

    func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let urls = draggedFileURLs(sender)
        guard !urls.isEmpty else { return false }
        flash?()
        copyFilesIntoShare(urls, report: report)
        return true
    }
}
