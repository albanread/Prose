# Prose packages — catalog of Haiku-native software

The [HaikuArchives](https://github.com/haikuarchives) collection (auto-indexed via `make_catalog.py`: 385 repos in `data/`, 375 listed here).
Criteria: native Be/Haiku API C++ only — no GTK/Qt/wx ports.
Codecs and POSIX/shell software are in scope.

Batch 1 = curated first wave to clone, cross-build (arm64) and install into the Prose image. Most have haikuports recipes, which `scripts/prosepkg build <port>` builds (see README.md).

## Editors And Ides (32)

| repo | description | batch 1 |
|---|---|---|
| [AuralIllusion](https://github.com/haikuarchives/AuralIllusion) | Audio editor for BeOS. (also known as "Ai4".) | |
| [BeAE](https://github.com/haikuarchives/BeAE) | Well featured audio editor for Haiku | |
| [BeAccessible](https://github.com/haikuarchives/BeAccessible) | Simple database viewer/editor. | |
| [BeBuilder](https://github.com/haikuarchives/BeBuilder) | GUI designer for BeOS. | |
| [BeFAR](https://github.com/haikuarchives/BeFAR) | A filemanager for Haiku. Classic NortonCommander look with modern multithreading inside. | |
| [BeInterfaceCreator](https://github.com/haikuarchives/BeInterfaceCreator) | WYSIWYG GUI designer. | |
| [BePDF](https://github.com/haikuarchives/BePDF) | BePDF is a PDF viewer. Besides viewing, it supports annotating and user-defined bookmarking for unencrypted PDFs. | **yes** |
| [BeTeX](https://github.com/haikuarchives/BeTeX) | LaTeX editor for BeOS/Zeta/Haiku | |
| [Brie](https://github.com/haikuarchives/Brie) | BeOS Rapid Integrated Environment (or BRIE for short) is an IDE for rapid development of BeOS / OBOS / Zeta applications. All code is generated in C/C++ using the BeOS API plus a few extensions. | |
| [CocoView](https://github.com/haikuarchives/CocoView) | Graphics/Slideshow Viewer for Haiku | |
| [EnglishEditorII](https://github.com/haikuarchives/EnglishEditorII) | An editor that doesn't look much like other text editors | |
| [Filer](https://github.com/haikuarchives/Filer) | The Filer is a powerful, flexible automatic file organizer. It is an implementation of the Sorting Chute idea conceived on the Glass Elevator mailing list for Haiku. | **yes** |
| [Globe](https://github.com/haikuarchives/Globe) | Webpage Editor for BeOS/Zeta. | |
| [HaikuLanguageBindings](https://github.com/haikuarchives/HaikuLanguageBindings) | Provides a way for scripting languages to access the Haiku API. | |
| [MeTOS](https://github.com/haikuarchives/MeTOS) | IDE for generate code-source from building interface. | |
| [MinimizeAll](https://github.com/haikuarchives/MinimizeAll) | MinimizeAll hides applications. | |
| [PDesigner](https://github.com/haikuarchives/PDesigner) | Br0ken GUI designer, was originally part of Paladin. | |
| [Paladin](https://github.com/haikuarchives/Paladin) | Paladin is an open source integrated development environment (IDE) for Haiku OS | **yes** |
| [Pe](https://github.com/haikuarchives/Pe) | Pe is a programmer's editor for Haiku | **yes** |
| [Q](https://github.com/haikuarchives/Q) | Analog sequence editor. | |
| [ResourceEdit](https://github.com/haikuarchives/ResourceEdit) | Resource Edit is a application for Haiku that lets you edit an app's resources. | **yes** |
| [Resourcer](https://github.com/haikuarchives/Resourcer) | A resource editor. | |
| [RestartDaemon](https://github.com/haikuarchives/RestartDaemon) | Allows application developers to restart their applications with a minimal amount of effort if an application is running. | |
| [SageBrush](https://github.com/haikuarchives/SageBrush) | Image editor for Haiku. | |
| [ScriptureGuide](https://github.com/haikuarchives/ScriptureGuide) | A Bible study tool based on the SWORD project that supports a wide variety of Bible translations and commentaries. | |
| [SilverWing](https://github.com/haikuarchives/SilverWing) | A Hotline client clone for BeOS. Hotline is a proprietary protocol of Hotline Communications that provides News, Chat and FTP. | |
| [Sisong](https://github.com/haikuarchives/Sisong) | SISONG editor/IDE source code. | |
| [SockHop](https://github.com/haikuarchives/SockHop) | Distributed network programming environment (API & shared library/server). | |
| [VirtualBeLive](https://github.com/haikuarchives/VirtualBeLive) | Open source video editing software for Haiku. | |
| [WhisperBeNet](https://github.com/haikuarchives/WhisperBeNet) | Whisper BeNet aims to provide a VoIP solution to the Haiku Platform. | |
| [Yab](https://github.com/haikuarchives/Yab) | Yab is a complete BASIC programming language for Haiku | |
| [hey-shoot](https://github.com/haikuarchives/hey-shoot) | Scripts for updating Haiku userguide screenshots | |

## Graphics And Media (61)

| repo | description | batch 1 |
|---|---|---|
| [AMC](https://github.com/haikuarchives/AMC) | The A'dam Music Composer. | |
| [Alsee](https://github.com/haikuarchives/Alsee) | A small image viewer with Previous/Next functions, drag and drop support and image associations. | |
| [Animator](https://github.com/haikuarchives/Animator) | Stop-motion animation tool. | |
| [ArmyKnife](https://github.com/haikuarchives/ArmyKnife) | ArmyKnife is an application that lets you edit the metadata of audio files | |
| [ArtPaint](https://github.com/haikuarchives/ArtPaint) | ArtPaint is a painting and image processing program. | **yes** |
| [AudioDancer](https://github.com/haikuarchives/AudioDancer) | A silly little program to experiment with BAudioSubscribers. | |
| [AudioMeter](https://github.com/haikuarchives/AudioMeter) | An audio meter. | |
| [BeDrift](https://github.com/haikuarchives/BeDrift) | Grabs images from all network traffic and displays them. | |
| [BeInYourStereo](https://github.com/haikuarchives/BeInYourStereo) | Be in your Stereo is a plugin to SoundPlay that scans your BFS volumes for digital music files. It builds a cross-referenced index of your collection based on Artist, Genre, Year, and Album BFS attributes, then serves up views of your track list and collection via HTTP. | |
| [BePhotoMagic](https://github.com/haikuarchives/BePhotoMagic) | BePhotoMagic is a resurrection of the abandoned Photon project and is intended to be a Photoshop-quality paint and image processing for BeOS. | |
| [Becasso](https://github.com/haikuarchives/Becasso) | Paint and imaging software for Haiku, originally written for BeOS by Sum Software. | |
| [CDPlayer](https://github.com/haikuarchives/CDPlayer) | Use your computer hardware to play CDs without the soundcard. | |
| [Colors](https://github.com/haikuarchives/Colors) | Colors! is a color picker like that in Adobe Photoshop. | |
| [Cortex](https://github.com/haikuarchives/Cortex) | Backup of the Cortex CVS repo. NOTE: Only here for "git blame" usage, if you want Cortex's source you can find it in Haiku's repository. | |
| [DesktopDrawer](https://github.com/haikuarchives/DesktopDrawer) | Temporary desktop container for files & folders | |
| [DocumentViewer](https://github.com/haikuarchives/DocumentViewer) | DocumentViewer is a Viewer supporting PDF and DJVU Files. | |
| [DrawButton](https://github.com/haikuarchives/DrawButton) | This function draws an empty BButton into a view. This is useful when you want to create a BPictureButton that looks BeOS-like. | |
| [EggsControl](https://github.com/haikuarchives/EggsControl) | Play multiple audio files at once. | |
| [FontBoy](https://github.com/haikuarchives/FontBoy) | A small application to show your installed fonts in Haiku | **yes** |
| [HPGSTranslator](https://github.com/haikuarchives/HPGSTranslator) | Translator for HP-GL/2 vector graphics files. | |
| [HaikuMIDILogger](https://github.com/haikuarchives/HaikuMIDILogger) |  | |
| [HaikuOnAStick](https://github.com/haikuarchives/HaikuOnAStick) | Windows application for easy getting a raw image to one or more removable device such as USB. | |
| [Hare](https://github.com/haikuarchives/Hare) | Haiku Audio Ripper/Encoder. | |
| [Hustler](https://github.com/haikuarchives/Hustler) | Audio player that runs in the Deskbar. | |
| [HyperStudio](https://github.com/haikuarchives/HyperStudio) | HyperStudio is a multitrack audio recording and editing suite with an easy to master graphical user interface. | |
| [ImageMounter](https://github.com/haikuarchives/ImageMounter) | Mounts filesystem-images by right-clicking. | |
| [InterWave](https://github.com/haikuarchives/InterWave) | A driver for AMD InterWave-based sound cards. | |
| [InternalMIDI](https://github.com/haikuarchives/InternalMIDI) | Creates a MIDI (MidiKit2) node for the internal General MIDI synthesizer of BeOS/Haiku. | |
| [KonaKoder](https://github.com/haikuarchives/KonaKoder) | CD ripper and MP3 encoder. | |
| [Lava](https://github.com/haikuarchives/Lava) | The Disc Burning Suite for Haiku | |
| [LibImageManip](https://github.com/haikuarchives/LibImageManip) | Shared library for image manipulation add-ons. | |
| [LibWalter](https://github.com/haikuarchives/LibWalter) | A supplementary collection of classes and controls that are sorely missing from the BeOS API, such as spinners, expanders, color and font pickers, and others. | **yes** |
| [MeV](https://github.com/haikuarchives/MeV) | A MIDI/music sequencer | **yes** |
| [MidiKeyboard](https://github.com/haikuarchives/MidiKeyboard) | An onscreen midi keyboard. | |
| [MidiMonitor](https://github.com/haikuarchives/MidiMonitor) | MidiKit1 events monitor. | |
| [MidiSynth](https://github.com/haikuarchives/MidiSynth) | A simple software MIDI keyboard using  Haiku's built-in synthesizer | **yes** |
| [MidiWorld](https://github.com/haikuarchives/MidiWorld) | MIDI player that can also modify tempo and transpose MIDI files. | |
| [MusicManager](https://github.com/haikuarchives/MusicManager) | A simple music manager. | |
| [NanoDot](https://github.com/haikuarchives/NanoDot) | A rather minimal pattern-based MIDI sequencer for BeOS. | |
| [OpenTracker](https://github.com/haikuarchives/OpenTracker) | Backup of the OpenTracker CVS repo. NOTE: Only here for "git blame" usage, if you want Tracker or Deskbar's source you can find it in Haiku's repository. | |
| [PDFWriter](https://github.com/haikuarchives/PDFWriter) | A printer driver that writes PDF files instead of sending data to a printer. | |
| [PecoBeat](https://github.com/haikuarchives/PecoBeat) | PecoBeat is a beat sequencer which uses the internal MIDI synth of the BeOS | **yes** |
| [PhotoGrabber](https://github.com/haikuarchives/PhotoGrabber) | PhotoGrabber is an application that downloads/deletes pictures from USB digital cameras | |
| [Photon](https://github.com/haikuarchives/Photon) | Photon / Natural Paint, a painting program | |
| [QScope](https://github.com/haikuarchives/QScope) | A digital oscilloscope for the ADC and DAC sound streams. | |
| [RalfTracker](https://github.com/haikuarchives/RalfTracker) | A sound tracker. | |
| [RusLAMEgui](https://github.com/haikuarchives/RusLAMEgui) | GUI for LAME. | |
| [ScannerBe](https://github.com/haikuarchives/ScannerBe) | ScannerBe is an API for applications that use image capture devices. | |
| [Sequitur](https://github.com/haikuarchives/Sequitur) | A Haiku-native MIDI sequencer with a MIDI processing add-on architecture. | **yes** |
| [SimplyVorbis](https://github.com/haikuarchives/SimplyVorbis) | A program to turn CDs into digital music files - MP3 or Ogg Vorbis | |
| [SoundMangler](https://github.com/haikuarchives/SoundMangler) | A digital audio filtering application. | |
| [SpeakIt](https://github.com/haikuarchives/SpeakIt) | This program reads the typed in text and searches a folder called 'words' for a sound file that corresponds to each individual word in the sentence. | |
| [Spirograph](https://github.com/haikuarchives/Spirograph) | A tool for drawing spiral curves. | |
| [StreamRadio](https://github.com/haikuarchives/StreamRadio) | Haiku-native application to search for and listen to internet radio stations. | **yes** |
| [TheAwesomeResizer](https://github.com/haikuarchives/TheAwesomeResizer) | Allows quick dynamic resizing of any translator-supported image. | **yes** |
| [UltraEncode](https://github.com/haikuarchives/UltraEncode) | CD audio extraction program. | |
| [UselessSoundplayPlugins](https://github.com/haikuarchives/UselessSoundplayPlugins) | Some SoundPlay visualization plugins. | |
| [WakeUp](https://github.com/haikuarchives/WakeUp) | WakeUp is a simple application that will play a given sound at a specified interval. | **yes** |
| [XRayDock](https://github.com/haikuarchives/XRayDock) | A transparent dock that can work with most of the images types supported by BeOS. | |
| [ZXTranslator](https://github.com/haikuarchives/ZXTranslator) | Translator add-on to load ZX Spectrum images | |
| [mp3tag](https://github.com/haikuarchives/mp3tag) | This is a Tracker add-on which edits mp3 tags (ID3-tags). You can use it on single files, but the real functionality you'll find if you run it on multiple files. | |

## Utilities (80)

| repo | description | batch 1 |
|---|---|---|
| [256](https://github.com/haikuarchives/256) | Colour utility for programmers | |
| [A-Book](https://github.com/haikuarchives/A-Book) | A small calendar application with reminders. | |
| [AESAddOn](https://github.com/haikuarchives/AESAddOn) | Tracker add-on to encrypt files with AES 128, 192, and 256 bit encryption | |
| [Album](https://github.com/haikuarchives/Album) | Album is a file browsing and tagging utility for BeOS and compatibles. | **yes** |
| [Archiver](https://github.com/haikuarchives/Archiver) | Archiver is meant to be Zip-O-Matic replacement. | **yes** |
| [AttribExplorer](https://github.com/haikuarchives/AttribExplorer) | AttribExplorer is a small utility that let you explore nodes' attributes under BeOS on BeFS drives. | |
| [BDH-Calc](https://github.com/haikuarchives/BDH-Calc) | 64bit programmer's calculator. | |
| [BSnow](https://github.com/haikuarchives/BSnow) | Winter weather for BeOS. | |
| [BeCalc](https://github.com/haikuarchives/BeCalc) | Desktop scientific calculator with many features. | |
| [BeIndexed](https://github.com/haikuarchives/BeIndexed) | BeIndexed searches all your files and builds a database with their content. You can then search for files by content, like on Google etc. | |
| [BeOhms](https://github.com/haikuarchives/BeOhms) | A simple Ohm's Law calculator. | |
| [Beacon](https://github.com/haikuarchives/Beacon) | Full text indexing and search tool for Haiku. | |
| [Borg](https://github.com/haikuarchives/Borg) | The Be Organizer, a calendar/organizer similar to KOrganizer. | |
| [Calc](https://github.com/haikuarchives/Calc) | A very simple scientific calculator. | **yes** |
| [Calendar](https://github.com/haikuarchives/Calendar) | :calendar: A native Calendar application for Haiku. | **yes** |
| [CommandTimer](https://github.com/haikuarchives/CommandTimer) | Runs a specified command after the timer runs out. | |
| [ConvertToLF](https://github.com/haikuarchives/ConvertToLF) | Tracker add-on to remove all carriage returns from text files. | |
| [CookingTimer](https://github.com/haikuarchives/CookingTimer) | A simple application which counts down to zero. | |
| [CopyNameToClipboard](https://github.com/haikuarchives/CopyNameToClipboard) | Tracker add-on that does exactly what the name says: It copies file names of the selected files (together with their paths) into the clipboard. | **yes** |
| [CoveredCalc](https://github.com/haikuarchives/CoveredCalc) | A desktop calculator with a skinnable interface. | |
| [DOCTranslator](https://github.com/haikuarchives/DOCTranslator) | Translator for Microsoft Word documents. Uses Antiword | |
| [DebugMonitor](https://github.com/haikuarchives/DebugMonitor) | This tiny piece of software improves the reliability and maintainability of your code. | |
| [DeskNotes](https://github.com/haikuarchives/DeskNotes) | A tool to put simple sticky notes on the Desktop | **yes** |
| [DeskbarEyes](https://github.com/haikuarchives/DeskbarEyes) | DeskbarEyes is a Desktop applet that puts a pair of eyes in your deskbar that follow your cursor | |
| [DockBert](https://github.com/haikuarchives/DockBert) | Deskbar modification, it adds a dock to your deskbar where you may have shortcuts organized in "tabs", a tab of the running apps and some other general eyecandy. | |
| [EMFTranslator](https://github.com/haikuarchives/EMFTranslator) | Data translator for Windows Extended Metafile format. | |
| [EXRTranslator](https://github.com/haikuarchives/EXRTranslator) | Haiku EXR translator | |
| [Eventual](https://github.com/haikuarchives/Eventual) | A personal time management system for Haiku. | |
| [Flyab](https://github.com/haikuarchives/Flyab) | Implementation of Yab in FLTK for use on non-Be systems. | |
| [Fortuna](https://github.com/haikuarchives/Fortuna) | Fortuna is a nice-looking graphical program which displays a fortune when your system starts. | |
| [GIGOcalc](https://github.com/haikuarchives/GIGOcalc) | A minimalist's expression calculator. | |
| [Graph](https://github.com/haikuarchives/Graph) | A simple graphing calculator for BeOS. | |
| [HexRpnCalc](https://github.com/haikuarchives/HexRpnCalc) | Hex-based reverse-polish-notation calculator. | |
| [Indices](https://github.com/haikuarchives/Indices) | Display information about the indices that the BFS filesystem maintains. | |
| [InterfaceElements](https://github.com/haikuarchives/InterfaceElements) | Attila Mezei's Interface Elements. This library helps you to instantiate window archives. Don't use for new code, here because some old apps require it. | |
| [KtCalc](https://github.com/haikuarchives/KtCalc) | Program to calculate the factor of stresses' concentration (Kt). | |
| [Lingua](https://github.com/haikuarchives/Lingua) | A multiple language translation utility. | **yes** |
| [LockWorkstation](https://github.com/haikuarchives/LockWorkstation) | 🔒 Lock your Haiku workstation | |
| [MAR](https://github.com/haikuarchives/MAR) | MAR is a command line utility for the BeOS platform. MAR will read flattened message files, file attributes, or file resources and output readable, editable XML. | |
| [MemoChip](https://github.com/haikuarchives/MemoChip) | A simple desktop memo tool, similar to post-it notes. | |
| [MoleSvn](https://github.com/haikuarchives/MoleSvn) | MoleSvn is a SVN client frontend for the BeOS/Zeta operating system, implemented as a Tracker extension. | |
| [OpenBeOS](https://github.com/haikuarchives/OpenBeOS) | Backup of the OpenBeOS CVS repo from before the 2002 restructuring (after which history is preserved in Haiku's main repository.) | |
| [OpenBinder](https://github.com/haikuarchives/OpenBinder) | Cross-platform inter-process communication. | |
| [OpenTargetFolder](https://github.com/haikuarchives/OpenTargetFolder) | This is a Tracker add-on that opens a Tracker window for the folder the selected link target lives in. | |
| [Organizer](https://github.com/haikuarchives/Organizer) | An organizer that helps you keep track on your appointments, notes and stuff. | |
| [Pad](https://github.com/haikuarchives/Pad) | A freeware notepad program. | |
| [PadBlocker](https://github.com/haikuarchives/PadBlocker) | A Haiku input_server filter to block the touchpad while you're typing. | |
| [PecoRename](https://github.com/haikuarchives/PecoRename) | PecoRename is a powerfull renaming utility, which allows you to rename many files according to a preset pattern. | **yes** |
| [PerfMonitor](https://github.com/haikuarchives/PerfMonitor) | PerfMonitor (Performance Monitor) is an X-style CPU monitor. | |
| [PonpokoDiff](https://github.com/haikuarchives/PonpokoDiff) | A GUI-based diff application for Haiku | **yes** |
| [QueryWatcher](https://github.com/haikuarchives/QueryWatcher) | This is a tiny little GUI application that monitors any regular Tracker queries and displays miniature indicator lights for the presence of results. | |
| [ReName](https://github.com/haikuarchives/ReName) | ReName! is a Tracker add-on that lets you batch-rename a set of files. | |
| [ReadBFSWindows](https://github.com/haikuarchives/ReadBFSWindows) | Read a drive with the BeFS file system on it from a Windows PC. | |
| [RpnCalc](https://github.com/haikuarchives/RpnCalc) | Reverse-polish-notation calculator. | **yes** |
| [Seeker](https://github.com/haikuarchives/Seeker) | Seeker is a revival of Pioneer, a Windows Explorer-like file manager from BeOS history. | |
| [Shredder](https://github.com/haikuarchives/Shredder) | File shredder tracker add-on. Can overwrite files up to 24 times. | |
| [Slayer](https://github.com/haikuarchives/Slayer) | Process controller (with extra features!) in a window | **yes** |
| [Snapshot](https://github.com/haikuarchives/Snapshot) | Ever wished you could go back to earlier versions of a file or folder? | |
| [SpCalculator](https://github.com/haikuarchives/SpCalculator) | The Silicon Peace Calculator. | |
| [Spiff](https://github.com/haikuarchives/Spiff) | 'spiff' - word diff. | |
| [SuperPrefs](https://github.com/haikuarchives/SuperPrefs) | SupePrefs is an application for Haiku 🖥️  providing Control Panel 🛠️ with Categorization 🏷️  and inner level Search 🔍  Part of GSoC '17. 👨‍💻 | |
| [SystemInfo](https://github.com/haikuarchives/SystemInfo) | A system monitor for Haiku. | |
| [TakeNotes](https://github.com/haikuarchives/TakeNotes) | Complex note-taking application | |
| [TaskManager](https://github.com/haikuarchives/TaskManager) | Cool NT-like system and process monitor. | **yes** |
| [TimeBomb](https://github.com/haikuarchives/TimeBomb) | A desktop alarm clock. | |
| [Tipster](https://github.com/haikuarchives/Tipster) | Application to display Haiku tips | **yes** |
| [Tolmach](https://github.com/haikuarchives/Tolmach) | BeOS port of KDictionary translation program for Linux | |
| [Toner](https://github.com/haikuarchives/Toner) | A tone generator for aligning levels in your playback system. | **yes** |
| [TrackerGrep](https://github.com/haikuarchives/TrackerGrep) | Tracker Grep is a simple Tracker add-on that lets you search through text files | |
| [TransPlus](https://github.com/haikuarchives/TransPlus) | A BeOS extension to the data-stream translation kit that is aimed at document file-types | |
| [USBCommander](https://github.com/haikuarchives/USBCommander) | A utility to watch or get information on USB devices. | |
| [USBDeskbar](https://github.com/haikuarchives/USBDeskbar) | A little tool that notifies when USB devices are connected or disconnected | |
| [UnifiedInputSystem](https://github.com/haikuarchives/UnifiedInputSystem) | Experimental new input system for Haiku, kind of based on HID. | |
| [Vandelay](https://github.com/haikuarchives/Vandelay) | A unit converter. | |
| [Weather](https://github.com/haikuarchives/Weather) | A weather app for Haiku | **yes** |
| [WheresMyMouse](https://github.com/haikuarchives/WheresMyMouse) | A small utility that highlighs where your mouse is on the screen. | |
| [WorkspaceNumber](https://github.com/haikuarchives/WorkspaceNumber) | Workspace is a Deskbar add-on that displays the current workspace number. | |
| [XCalc](https://github.com/haikuarchives/XCalc) | A more complex calculator. | |
| [XPMTranslator](https://github.com/haikuarchives/XPMTranslator) |  | |
| [haikuarchives.github.io](https://github.com/haikuarchives/haikuarchives.github.io) | Information on HaikuArchives and the software we archive | |

## Internet And Network (23)

| repo | description | batch 1 |
|---|---|---|
| [BeAIM](https://github.com/haikuarchives/BeAIM) | AIM chat client for BeOS. | |
| [BeDC](https://github.com/haikuarchives/BeDC) | BeDC is a peer to peer file sharing client for BeOS for the Direct Connect protocol. | |
| [BeMailDaemon](https://github.com/haikuarchives/BeMailDaemon) | BeMailDaemon, a.k.a. Mail Daemon Replacement a.k.a. MDR, is a complete replacement for BeOS R5's mail_daemon. (This has been merged into Haiku and is only here for historical purposes.) | |
| [BePPP](https://github.com/haikuarchives/BePPP) | PPPoE and PPtP client for BeOS under old-fashionded networking | |
| [BeServed](https://github.com/haikuarchives/BeServed) | Cross-platform network file sharing, designed around BeOS. | |
| [Beam](https://github.com/haikuarchives/Beam) | BEware, Another Mailer - an e-mail client for BeOS/Haiku | |
| [Beezer](https://github.com/haikuarchives/Beezer) | Beezer is a WinZip like archiver program for BeOS. (ARCHIVED, new repository at: https://github.com/Teknomancer/beezer) | |
| [BookmarkConverter](https://github.com/haikuarchives/BookmarkConverter) | Bookmark export tool for WebPositive | |
| [Bowser](https://github.com/haikuarchives/Bowser) | Bowser is an IRC client for BeOS that aims to be very easy to use, very elegant, and very stable. | |
| [FTPme](https://github.com/haikuarchives/FTPme) | ATracker add-on designed to let you quickly and easily send a selection of files or folders to another computer via FTP. | |
| [FtpPositive](https://github.com/haikuarchives/FtpPositive) | A simple graphical FTP client. | **yes** |
| [GmailNotifier](https://github.com/haikuarchives/GmailNotifier) | A simple Gmail Notifier. Made in YAB for Zeta, use wget and infopopper. | |
| [HaikuTwitter](https://github.com/haikuarchives/HaikuTwitter) | A native Twitter client for Haiku | |
| [IMKit](https://github.com/haikuarchives/IMKit) | Kit that connects to instant messaging networks such as Jabber, GoogleTalk, MSN, ICQ, AIM and Yahoo. There is a server and client applications that perform specific tasks and protocol add-ons. | |
| [Item](https://github.com/haikuarchives/Item) | A newsreader for BeOS. It is more or less modelled after MT-Newswatcher on the Mac. | |
| [Nettle](https://github.com/haikuarchives/Nettle) | DO NOT USE FOR NEW PROJECTS. Nettle is an object oriented abstraction over network socket communications. | |
| [NewsBe](https://github.com/haikuarchives/NewsBe) | A NNTP / Usenet client for the BeOS. | |
| [RobinHood](https://github.com/haikuarchives/RobinHood) | A free HTTP/1.1 web server for BeOS. It is an original work designed from the ground up for BeOS; it contains no ported code. | |
| [Scooby](https://github.com/haikuarchives/Scooby) | Scooby is an open source full featured BeOS native e-mail client. | |
| [Stamina](https://github.com/haikuarchives/Stamina) | A handy utility for capturing web sites onto your hard disk. You can browse the captured files offline through Charisma, a personal proxy server. | |
| [Vision](https://github.com/haikuarchives/Vision) | A native Haiku IRC client that is feature filled, fast, lightweight, and stable. | **yes** |
| [WebWatch](https://github.com/haikuarchives/WebWatch) | A port of Swatch's Internet Time utility, a universal time format that eliminates time zones and geographical borders. | |
| [libHTTP](https://github.com/haikuarchives/libHTTP) | DO NOT USE FOR NEW PROJECTS. A library for managing HTTP servers and other stuff | |

## Games (27)

| repo | description | batch 1 |
|---|---|---|
| [BShisen](https://github.com/haikuarchives/BShisen) | BeOS tile matching game. | **yes** |
| [BabyBe](https://github.com/haikuarchives/BabyBe) | A simple game for small childen. | **yes** |
| [BeBattle](https://github.com/haikuarchives/BeBattle) | A 1 or 2 players board strategy game in which you attempt to completly destroy your opponent's units. | |
| [BeCheckers](https://github.com/haikuarchives/BeCheckers) | BeCheckers is a simple checkers game developed for two players. The game conforms to almost all ACF (American Checker Federation) rules. | |
| [BeLife](https://github.com/haikuarchives/BeLife) | BeLife is a BeOS/Zeta/Haiku implementation of the 'Conway's game of Life' math model. | **yes** |
| [BeMines](https://github.com/haikuarchives/BeMines) | A themable, open-source rendition of Minesweeper | |
| [BeNetTris](https://github.com/haikuarchives/BeNetTris) | BeNetTris is a Tetris client and server. | |
| [BePuyo](https://github.com/haikuarchives/BePuyo) | A Tetris/GemDrop-like game. Development moved to: https://codeberg.org/nikisoft/BePuyo | |
| [BeSol](https://github.com/haikuarchives/BeSol) | Easily create and play games of solitaire. | **yes** |
| [BeSpider](https://github.com/haikuarchives/BeSpider) | A spider solitaire clone for Haiku. | **yes** |
| [BeVexed](https://github.com/haikuarchives/BeVexed) | A rendition of the popular time-wasting game TetraVex | **yes** |
| [Bing](https://github.com/haikuarchives/Bing) | Bing is a small Pong clone that does not run in a window but uses windows to display rectangles onscreen. | |
| [Bong](https://github.com/haikuarchives/Bong) | BeOS version of Pong. | **yes** |
| [Connect4](https://github.com/haikuarchives/Connect4) | The game of Connect-4  is played by players taking turns dropping pieces down a column | |
| [Conway](https://github.com/haikuarchives/Conway) | Silly Conway's Game of Life emulator | **yes** |
| [Cygnus](https://github.com/haikuarchives/Cygnus) | A simple OpenGL game for teaching entry-level programmers. | **yes** |
| [DwarfEngine](https://github.com/haikuarchives/DwarfEngine) | An OpenGL-based game engine for BeOS and Windows from 2001. | |
| [Dynamate](https://github.com/haikuarchives/Dynamate) | A game about merging bombs of different colors. | **yes** |
| [EggChess](https://github.com/haikuarchives/EggChess) | Chess game | |
| [Haiku2048](https://github.com/haikuarchives/Haiku2048) | Clone of the popular 2048 game for Haiku | |
| [HexVexed](https://github.com/haikuarchives/HexVexed) | A Hexagon game based on DarkWyrm's BeVexed, a maddeningly-addictive puzzle game, which was inspired by the Linux game TetraVex. | **yes** |
| [JoystickUtilizer](https://github.com/haikuarchives/JoystickUtilizer) | Finally you can play games with a joystick that didn't support one so far: JoystickUtilizer converts joystick signals into keyboard events. Very useful for emulators! | |
| [LightsOff](https://github.com/haikuarchives/LightsOff) | Lights Off! is a rendition of the original handheld game "Lights Out!", manufactured by Tiger Electronics. | |
| [Minesweeper](https://github.com/haikuarchives/Minesweeper) | Minesweeper for Haiku | **yes** |
| [Puri](https://github.com/haikuarchives/Puri) | Play chess in 2D or 3D. Against an engine (Stockfish) at different Level Skills, or play online against humans on FICS. | |
| [SlaveMind](https://github.com/haikuarchives/SlaveMind) | A MasterMind clone. | **yes** |
| [W6](https://github.com/haikuarchives/W6) | What Went Wrong? It's a World Wide War - a strategy game for BeOS. | |

## Demos And Samples (31)

| repo | description | batch 1 |
|---|---|---|
| [2dPhysicsDemo](https://github.com/haikuarchives/2dPhysicsDemo) | 2D-Demo: 2D physics demo. | |
| [3DMorph](https://github.com/haikuarchives/3DMorph) | 3D Morph is a screen saver for the BeOS. It shows some 3D objects that move and rotate on the screen. | |
| [BSOD](https://github.com/haikuarchives/BSOD) | The BSOD screensaver for Haiku | |
| [BeBitsUpdated](https://github.com/haikuarchives/BeBitsUpdated) | BeBits Updated is a Deskbar Replicant that will poll BeBits.com and look for new software. Its pretty self explanatory so download it and try it out. | |
| [BeSampleCode](https://github.com/haikuarchives/BeSampleCode) | Original BeOS developer sample code for learning the BeOS / Haiku API. | **yes** |
| [BeSwarm](https://github.com/haikuarchives/BeSwarm) | BeSwarm is the premier screen saver for BeOS. | |
| [BeVoxel](https://github.com/haikuarchives/BeVoxel) | BeVoxel is a small example of voxel BeOS with the use of direct access to the video memory. | |
| [BeWake](https://github.com/haikuarchives/BeWake) | A screensaver. | |
| [BinaryClock](https://github.com/haikuarchives/BinaryClock) | A binary clock application and screensaver | |
| [BrainWash](https://github.com/haikuarchives/BrainWash) | The color cycling hallucinogene screensaver. | |
| [CDButton](https://github.com/haikuarchives/CDButton) | A Deskbar replicant that can control CD players. | |
| [DateReplicant](https://github.com/haikuarchives/DateReplicant) | This is a simple replicant application. It shows date info. | **yes** |
| [DuckSaver](https://github.com/haikuarchives/DuckSaver) | Screensaver which transforms your screen into a duck pool. | |
| [FMedia](https://github.com/haikuarchives/FMedia) | F3C's media node library and sample add-ons. | |
| [FScreenSaver](https://github.com/haikuarchives/FScreenSaver) | A cool screensaver for Haiku. | |
| [GLcubesSaver](https://github.com/haikuarchives/GLcubesSaver) | A 3D Cubes screensaver. | |
| [KanjiSaver](https://github.com/haikuarchives/KanjiSaver) | (maybe) Displays Kanji characters in the screensaver...? | |
| [Konfetti](https://github.com/haikuarchives/Konfetti) | Konfetti is a little screensaver which draws (semi-transparent) confetti on your desktop. | |
| [License-Breaker](https://github.com/haikuarchives/License-Breaker) | Screensaver that disassembles the kernel. | |
| [Moe](https://github.com/haikuarchives/Moe) | Displays a cute mascot on the active window | |
| [NetPulse](https://github.com/haikuarchives/NetPulse) | A network monitoring replicant for Haiku. | |
| [Repliman](https://github.com/haikuarchives/Repliman) | Repliman is a simple replicant manager. If you have problems trying to remove stubborn replicants from your desktop or deskbar then this is the program for you!  http://www.kumo.it/beos/repliman | |
| [SheepSaver](https://github.com/haikuarchives/SheepSaver) | SheepSaver is a screen saver which draws sheep all over your desktop. | |
| [Space](https://github.com/haikuarchives/Space) | Space screensaver. | |
| [Spooky](https://github.com/haikuarchives/Spooky) | A Blanker module. | |
| [Substrate](https://github.com/haikuarchives/Substrate) | Xscreensaver's Substrate for Haiku. | |
| [TileSaver](https://github.com/haikuarchives/TileSaver) | Colorful screensaver that draws rectangles all over the screen. | |
| [VoiceBender](https://github.com/haikuarchives/VoiceBender) | Manually passes data from the A/D to the D/A and processes samples. | |
| [Wolle](https://github.com/haikuarchives/Wolle) | Wolle is a screensaver which draws a woollen string over your desktop - looks nice! :-) | |
| [WorkInMotion](https://github.com/haikuarchives/WorkInMotion) | Experimental screensaver for the Haiku operating system. | |
| [backslash_n](https://github.com/haikuarchives/backslash_n) | \n by Linefeed, a demo for BeOS. | |

## Libraries (12)

| repo | description | batch 1 |
|---|---|---|
| [ArpCommon](https://github.com/haikuarchives/ArpCommon) | This is a set of class libraries that we have created in the course of developing BeOS applications. We find them fairly useful, and are releasing them as open source in the hope that others will as well. | |
| [BeFree](https://github.com/haikuarchives/BeFree) | BeOS API and desktop on top of the Linux kernel. | |
| [BeGUI](https://github.com/haikuarchives/BeGUI) | BeGUI is a simple to use programmer's tool which allows for rapid development of a program. | |
| [BeMDI](https://github.com/haikuarchives/BeMDI) | BeOS/Haiku framework to create Windows-style multidocument applications. | |
| [CapitalBe](https://github.com/haikuarchives/CapitalBe) | CapitalBe is the premiere personal finance manager for Haiku. | **yes** |
| [CastleYankee](https://github.com/haikuarchives/CastleYankee) | Create your C++ classes in a Castle Yankee project, and it will generate the .h and .cpp files, sparing you from having to type the signatures of all member functions and instance variables twice. | |
| [EasyToolkitAndExtension](https://github.com/haikuarchives/EasyToolkitAndExtension) | A toolkit and extension on BeOS/Windows/POSIX, a little like the BeOS API on platforms other than BeOS. | |
| [Habid](https://github.com/haikuarchives/Habid) | D and C Bindings for the Haiku API. | |
| [SantasGiftBag](https://github.com/haikuarchives/SantasGiftBag) | DO NOT USE FOR NEW PROJECTS. Shareware (?) library that has some useful custom widgets in it. | |
| [Squirrel99](https://github.com/haikuarchives/Squirrel99) | A language based on LOGO for the BeOS/Haiku API | |
| [Tekhne](https://github.com/haikuarchives/Tekhne) | Tekhne is a reasonably compatible version of the BeOS API on top of Linux. | |
| [ZooKeeper](https://github.com/haikuarchives/ZooKeeper) | ZooKeeper is a glue application (or frontend) that lets you specify a shell command or script to be executed on a set of files. | |

## Misc (109)

| repo | description | batch 1 |
|---|---|---|
| [3DMov](https://github.com/haikuarchives/3DMov) | Play movies on 3D objects. | |
| [4Wins](https://github.com/haikuarchives/4Wins) | Variation on tic-tac-toe for Haiku. | |
| [8Dock](https://github.com/haikuarchives/8Dock) | Yet another dock for Haiku. | |
| [AVLDupTree](https://github.com/haikuarchives/AVLDupTree) | AVLDupTree is a set of C subroutines (not C++, so you can use it in drivers) that is useful for indexing a set of key/value pairs, using the key to find a matching value. | |
| [Aardwolf](https://github.com/haikuarchives/Aardwolf) | Generic MIME launcher for commandline apps. | |
| [AbiWord](https://github.com/haikuarchives/AbiWord) | The old AbiWord for BeOS. | |
| [AnalogPulse](https://github.com/haikuarchives/AnalogPulse) | CPU load factor analog gauge(s) | |
| [AreaWatch](https://github.com/haikuarchives/AreaWatch) | A memory utilization app that works by graphically displaying the output of listarea. | |
| [BGhostView](https://github.com/haikuarchives/BGhostView) | PostScript viewer for BeOS. | |
| [BeCJK](https://github.com/haikuarchives/BeCJK) | Chinese/Japanese/Korean input method for Haiku. | |
| [BeCheckPoint](https://github.com/haikuarchives/BeCheckPoint) |  | |
| [BeDVI](https://github.com/haikuarchives/BeDVI) | BeDVI is a DVI viewer based on xdvi. | |
| [BeDead](https://github.com/haikuarchives/BeDead) | A handy little program killer. | |
| [BeDrivers](https://github.com/haikuarchives/BeDrivers) | The place for Drivers for BeOS/Haiku/Zeta. | |
| [BeFax](https://github.com/haikuarchives/BeFax) | Send faxes on BeOS. | |
| [BeFull](https://github.com/haikuarchives/BeFull) | A full-screen app launcher for BeOS / Haiku. | |
| [BeGadu](https://github.com/haikuarchives/BeGadu) | DEPRECATED. Use TokuToku instead. | |
| [BeGet](https://github.com/haikuarchives/BeGet) | A GUI frontend to the command-line downloader 'wget'. | |
| [BeHugo](https://github.com/haikuarchives/BeHugo) | Hugo interpreter for BeOS. | |
| [BeLogin](https://github.com/haikuarchives/BeLogin) | A simple authorization mechanism for BeOS. | |
| [BeMetro](https://github.com/haikuarchives/BeMetro) | A simple metronome. | |
| [BeNormal](https://github.com/haikuarchives/BeNormal) | A small command line program designed to normalise wav files. | |
| [BePhoneBook](https://github.com/haikuarchives/BePhoneBook) | A phone book manager. | |
| [BePodder](https://github.com/haikuarchives/BePodder) | A media aggregator (and more) | |
| [BeRDP](https://github.com/haikuarchives/BeRDP) | GUI for RDesktop. | |
| [BeRestart](https://github.com/haikuarchives/BeRestart) |  | |
| [BeShare](https://github.com/haikuarchives/BeShare) | Easy filesharing for Haiku | |
| [Bme](https://github.com/haikuarchives/Bme) | The most feature complete MSN messenger program for BeOS and derivatives. | |
| [CherryBlossom](https://github.com/haikuarchives/CherryBlossom) | An iTunes-style media player for Haiku. | |
| [ClipUp](https://github.com/haikuarchives/ClipUp) | Extends your clipboard with some nice features: A history function and a "restore-content-after-reboot" function. | |
| [Clue](https://github.com/haikuarchives/Clue) | The most comprehensive BeOS GUI tracing tool, allowing inspection on almost all of the BeOS native objects. | |
| [DataPlus](https://github.com/haikuarchives/DataPlus) | DataPlus. | |
| [DeeperPeople](https://github.com/haikuarchives/DeeperPeople) | Extended version of the People application. | |
| [Delay64](https://github.com/haikuarchives/Delay64) | N64 emulator written for BeOS. Doesn't even play Mario 64 yet | |
| [DetMatrix](https://github.com/haikuarchives/DetMatrix) | A multi-threaded any-size matrix solver. | |
| [ExeCoffSky](https://github.com/haikuarchives/ExeCoffSky) | Also known as WinBe or Win4Be, ExeCoffSky aimed to run Windows applications in BeOS. | |
| [ExpressionEvaluator](https://github.com/haikuarchives/ExpressionEvaluator) | A simple expression evaluator. | |
| [FeedKit](https://github.com/haikuarchives/FeedKit) | The Feed Kit. | |
| [FilWip](https://github.com/haikuarchives/FilWip) | FilWip is the BeOS Clean-Up Manager replacement app. | |
| [FileCropper](https://github.com/haikuarchives/FileCropper) | Truncates files of any kind. Cuts off everything from a given offset (bytes) to the end of the file. | |
| [FilterResponse](https://github.com/haikuarchives/FilterResponse) | Shows you the frequency response and phase response of a second order IIR filter. | |
| [Finance](https://github.com/haikuarchives/Finance) | Personal finance application. | |
| [FolderShaper](https://github.com/haikuarchives/FolderShaper) | Retro-fit looks and layout on your existing folders, using this template-based tool. | |
| [FunctionPlotter](https://github.com/haikuarchives/FunctionPlotter) | Plots an abitrary number of functions in parameter form. | |
| [GenesisCommander](https://github.com/haikuarchives/GenesisCommander) | Genesis Commander is a full featured file manager for Haiku | |
| [Gloom3D](https://github.com/haikuarchives/Gloom3D) | Gloom - port of the Boom3D Quake-like engine to BeOS. | |
| [GuitarMaster](https://github.com/haikuarchives/GuitarMaster) | Guitar Master is a Frets-on-Fire clone for Haiku. | |
| [GuitarTuner](https://github.com/haikuarchives/GuitarTuner) | Guitar tuner. | |
| [HaikuLocalization](https://github.com/haikuarchives/HaikuLocalization) | Haiku Localization. | |
| [HaikuPlot](https://github.com/haikuarchives/HaikuPlot) | Haiku App for gnuplot | |
| [HaikuThemeManager](https://github.com/haikuarchives/HaikuThemeManager) | Zeta-compatible Theme Manager. | |
| [HaikuUsabilityTools](https://github.com/haikuarchives/HaikuUsabilityTools) | A collection of utilities to make Haiku useful for ordinary use. | |
| [HexKeycode](https://github.com/haikuarchives/HexKeycode) | Hex Key Code Finder is a program written to make easy the creation of a new keymap or the personalization of an existing one. | |
| [HeyModule](https://github.com/haikuarchives/HeyModule) | heymodule for python | |
| [HobbitTools](https://github.com/haikuarchives/HobbitTools) | Tools for working with stuff from BeOS on the AT&T Hobbit. | |
| [HoiPolloi](https://github.com/haikuarchives/HoiPolloi) |  | |
| [JamMin](https://github.com/haikuarchives/JamMin) | Graphic interface that lets you manage a C/C++ project and use "Jam" as the build tool. | |
| [Kagari](https://github.com/haikuarchives/Kagari) | An intelligent launcher: not "recently used applications", but also "frequently used applications." | |
| [KeyCursor](https://github.com/haikuarchives/KeyCursor) | Using a Keyboard as a Pointing Device | |
| [KeymapSwitcher](https://github.com/haikuarchives/KeymapSwitcher) | An easy to use Keymap Switcher | |
| [KoreanIM](https://github.com/haikuarchives/KoreanIM) | Korean Input Method for BeOS/Haiku | |
| [LaunchPad](https://github.com/haikuarchives/LaunchPad) | A simple BeOS application/file/folder launcher. | |
| [Maps](https://github.com/haikuarchives/Maps) | A maps application for Haiku. | |
| [Masterpiece](https://github.com/haikuarchives/Masterpiece) | New way to create opendocument compatible books/documents. | |
| [Melt](https://github.com/haikuarchives/Melt) | A GUI for cdrecord and mkhybrid | |
| [Mosaic](https://github.com/haikuarchives/Mosaic) | Mosaic creates a mosaic reproduction of any picture using frames captured live from your TV input card. | |
| [MrPeeps](https://github.com/haikuarchives/MrPeeps) | A free, simple, fast, easy-to-use, studly open source contact manager designed to make entering and looking up information as easy as possible | |
| [NightAndDay](https://github.com/haikuarchives/NightAndDay) | Automatic desktop color changer. | |
| [Niue](https://github.com/haikuarchives/Niue) | Niue is an easy to use but powerful development environment. | |
| [PPViewer](https://github.com/haikuarchives/PPViewer) | Decrunches all types of Amiga PowerPacker files, including encrypted files. | |
| [Pager](https://github.com/haikuarchives/Pager) | A small app for reading text files. | |
| [PhantomLimb](https://github.com/haikuarchives/PhantomLimb) | PhantomLimb is a little program to generate tones. | **yes** |
| [PlottingTools](https://github.com/haikuarchives/PlottingTools) | Various plotting tools. | |
| [Randomizer](https://github.com/haikuarchives/Randomizer) | Random symbol sequence (e.g. password) generator for Haiku | |
| [Recibe](https://github.com/haikuarchives/Recibe) | Recibe is a no-nonsense, simple-but-friendly open source recipe manager for BeOS. | |
| [RefLine](https://github.com/haikuarchives/RefLine) | Bibliographic References Manager. | |
| [Remember](https://github.com/haikuarchives/Remember) | Tiny app to notify you | |
| [RemoteControl](https://github.com/haikuarchives/RemoteControl) | RemoteControl is an application that allows the user to control a remote PC as if he was seating in front of it. It can be useful if you have a PC with no screen, keyboard or mouse connected, or simply if it is too far away. | |
| [Rez](https://github.com/haikuarchives/Rez) | Resource compiler | |
| [RunProgram](https://github.com/haikuarchives/RunProgram) | Lets you run just about any app quickly from the command line. | |
| [SKey](https://github.com/haikuarchives/SKey) | An MD4 and MD5 s/key generator. | |
| [ShisenSho](https://github.com/haikuarchives/ShisenSho) | BeOS-Port of KSHISEN | |
| [SineGenerator](https://github.com/haikuarchives/SineGenerator) | Sine tone generator with adjustable frequency. | |
| [StampTV](https://github.com/haikuarchives/StampTV) | stampTV is the most full-featured TV application available for BeOS. | |
| [StrokeIt](https://github.com/haikuarchives/StrokeIt) | Supposedly a PDA input driver. | |
| [TeXEdit](https://github.com/haikuarchives/TeXEdit) | TexEdit is a Syntax coloring text app for tex. | |
| [TimeCop](https://github.com/haikuarchives/TimeCop) | With TimeCop you can view 4 statistics showing BeOS uptime. | |
| [TimeTracker](https://github.com/haikuarchives/TimeTracker) | Allows you to log time spent on tasks. | |
| [TokuToku](https://github.com/haikuarchives/TokuToku) | A native Gadu-Gadu messaging client for Haiku. | |
| [Torrentor](https://github.com/haikuarchives/Torrentor) | BitTorrent client for Haiku | |
| [TraX](https://github.com/haikuarchives/TraX) | Find files by name AND location, unlike Haiku's Find. | |
| [TrackGit](https://github.com/haikuarchives/TrackGit) |  | |
| [TrackerScript](https://github.com/haikuarchives/TrackerScript) | This hybrid Application/Tracker addon executes a script (stored in attribute "script") with the selected files as arguments. | |
| [UniversalScroller](https://github.com/haikuarchives/UniversalScroller) | Enhanced input methods for BeOS and Haiku | |
| [VMwareAddons](https://github.com/haikuarchives/VMwareAddons) | VMwareAdd-ons is a set of tools to enhance interaction with Haiku running in VMware | |
| [VNCViewer](https://github.com/haikuarchives/VNCViewer) | Viewer for VNC remote desktop connections. | |
| [VWGet](https://github.com/haikuarchives/VWGet) | A visual version for GNU's wget for the BeOS | |
| [WinRC2Be](https://github.com/haikuarchives/WinRC2Be) | Converts MS Visual Studio 6.0 RC (Resource files) into Be Interface Kit C++ source code. | |
| [XRS](https://github.com/haikuarchives/XRS) | A simple Rhythm Station. You can use it to create loops. | |
| [YasirsJunkyard](https://github.com/haikuarchives/YasirsJunkyard) | A collection of small BeOS programs and plugins. | |
| [ZumisIcons](https://github.com/haikuarchives/ZumisIcons) | Mirror of Zumi's icons | |
| [ac3_decoder](https://github.com/haikuarchives/ac3_decoder) | AC3 decoder media add-on. | |
| [app2png](https://github.com/haikuarchives/app2png) | Turn legacy BeOS app icons into PNGs | |
| [ffmpegGUI](https://github.com/haikuarchives/ffmpegGUI) | GUI for FFmpeg | |
| [fusesmb-haiku](https://github.com/haikuarchives/fusesmb-haiku) | Haiku port of fusesmb | |
| [pi](https://github.com/haikuarchives/pi) | "pi" - a user level packet injector | |
| [sanity](https://github.com/haikuarchives/sanity) | A graphical Haiku & BeOS scanner application, using SANE. | |
| [wayland](https://github.com/haikuarchives/wayland) |  | |
| [xvid_decoder](https://github.com/haikuarchives/xvid_decoder) | XVID decoder media add-on. | |

## Excluded (foreign toolkits / out of scope)

B
e
O
S
T
i
L
i
n
k
,
 
B
u
t
t
e
r
f
l
y
,
 
F
a
k
e
V
i
d
e
o
P
l
u
g
i
n
s
,
 
F
l
y
i
n
g
T
r
o
l
l
,
 
M
a
n
a
b
u
,
 
N
e
t
O
p
t
i
m
i
s
t
,
 
P
e
e
k

