#!/bin/bash
# Build the Prose installer: a disk image holding one thing, "Installer",
# signed, notarized and stapled, that opens on a double click with nothing to
# dismiss.
#
#   scripts/make-installer.sh
#   scripts/make-installer.sh --no-notarize      # for a local look
#
# Prose.app travels inside the installer (Contents/Resources/Prose.app), so the
# disk image has no second item and nobody has to guess whether to drag it or
# open it. The installer chooses what to put where:
#
#   /Applications/Prose.app                                  the application
#   ~/Library/Application Support/Prose/Machines/Prose.image  the machine
#   ~/Documents/HostFS                                        made by Prose, never touched
#
# and inside the machine, put-userguide.sh adds /boot/home/Desktop/
# "Prose User Guide.pdf" — the guide ships with the thing it describes.
#
# and it can take them away again. Drag-and-drop could only ever replace the
# application, which is how a newer Prose came to boot an older machine.
#
# Three notarizations, in this order and for a reason: Prose.app is notarized
# and stapled BEFORE it goes inside, so the copy the installer puts in
# /Applications carries its own ticket and passes Gatekeeper on a Mac that is
# offline. Stapling only the outer container would leave that copy ticketless.
#
# Signing wants a Developer ID Application certificate, and notarizing wants a
# keychain profile, made once with
#   xcrun notarytool store-credentials macvm --apple-id … --team-id … --password …
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/installer"
IMAGE="${PROSE_IMAGE:-/Volumes/HaikuSrc/haiku/haiku-mmc.image}"
PROFILE="${PROSE_NOTARY_PROFILE:-macvm}"
VERSION="${PROSE_VERSION:-$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)}"
SHORT="${PROSE_SHORT_VERSION:-0.1}"

NOTARIZE=1
for arg in "$@"; do
	case "$arg" in
		--no-notarize) NOTARIZE=0 ;;
		*) echo "unknown option $arg" >&2; exit 64 ;;
	esac
done

say() { printf '\n== %s\n' "$*"; }
die() { printf '\n!! %s\n' "$*" >&2; exit 1; }

APP_ID="${PROSE_SIGN_APP:-$(security find-identity -v -p codesigning 2>/dev/null \
	| sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)}"
[ -n "$APP_ID" ] || die "no Developer ID Application certificate in the keychain.
   Xcode > Settings > Accounts > Manage Certificates > + > Developer ID Application."

notarize() {  # what it is, path
	[ "$NOTARIZE" = 1 ] || { say "not notarizing $1 (--no-notarize)"; return 0; }
	say "notarizing $1 with the “${PROFILE}” profile (this waits for Apple)"
	local submission="$2"
	if [ -d "$2" ]; then			# a bundle is submitted zipped
		submission="$OUT/$(basename "$2").zip"
		ditto -c -k --keepParent "$2" "$submission"
	fi
	xcrun notarytool submit "$submission" --keychain-profile "$PROFILE" --wait \
		|| die "notarizing $1 failed. For the reasons:
   xcrun notarytool log <submission-id> --keychain-profile $PROFILE"
	[ -d "$2" ] && rm -f "$submission"
	xcrun stapler staple "$2"
	xcrun stapler validate "$2" >/dev/null || die "$1 would not staple"
}

# ------------------------------------------------------------------- build
say "building"
"$ROOT/tools/build.sh"
[ -d "$ROOT/build/Prose.app" ] || die "no build/Prose.app"
[ -d "$ROOT/build/Installer.app" ] || die "no build/Installer.app"
[ -f "$IMAGE" ] || die "no machine at $IMAGE (set PROSE_IMAGE)"

rm -rf "$OUT"
mkdir -p "$OUT/dmg"
APP="$OUT/Prose.app"
INSTALLER="$OUT/dmg/Installer.app"
cp -R "$ROOT/build/Prose.app" "$APP"
cp -R "$ROOT/build/Installer.app" "$INSTALLER"

say "putting the machine inside Prose ($(du -m "$IMAGE" | cut -f1) MiB)"
cp "$IMAGE" "$APP/Contents/Resources/prose.image"

# the guide travels inside the machine it describes: /boot/home/Desktop from
# the first boot. Runs on the copy above, before anything is signed.
say "putting the user guide inside the machine"
bash "$ROOT/scripts/put-userguide.sh" "$APP/Contents/Resources/prose.image"

for bundle in "$APP" "$INSTALLER"; do
	/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $SHORT" \
		-c "Set :CFBundleVersion $VERSION" "$bundle/Contents/Info.plist" >/dev/null
done

# ------------------------------------------------------- sign and notarize
# Hardened runtime is what notarization requires; the virtualization
# entitlement works inside it. Only Prose needs that entitlement.
say "signing Prose as “${APP_ID}”"
codesign --force --timestamp --options runtime \
	--entitlements "$ROOT/tools/common/vz.entitlements" --sign "$APP_ID" "$APP"
codesign --verify --deep --strict "$APP"
notarize "Prose" "$APP"

say "putting Prose inside the installer"
mkdir -p "$INSTALLER/Contents/Resources"
cp -R "$APP" "$INSTALLER/Contents/Resources/Prose.app"

say "signing the installer as “${APP_ID}”"
# inside out: the nested application is already signed and stapled, and sealing
# the installer over it must not disturb that
codesign --force --timestamp --options runtime --sign "$APP_ID" "$INSTALLER"
codesign --verify --deep --strict "$INSTALLER"
[ "$NOTARIZE" = 0 ] || \
	xcrun stapler validate "$INSTALLER/Contents/Resources/Prose.app" >/dev/null \
	|| die "the nested Prose lost its ticket when the installer was signed"
notarize "the installer" "$INSTALLER"

# ---------------------------------------------------------------- package
PRODUCT="$OUT/Prose-$VERSION.dmg"
say "building $(basename "$PRODUCT")"
hdiutil create -quiet -ov -format UDZO -srcfolder "$OUT/dmg" -volname "Install Prose" "$PRODUCT"
codesign --force --timestamp --sign "$APP_ID" "$PRODUCT"
notarize "the disk image" "$PRODUCT"

say "what Gatekeeper makes of it"
spctl --assess --type open --context context:primary-signature -vv "$PRODUCT" 2>&1 | sed 's/^/   /'

rm -rf "$APP"
say "done: $PRODUCT  ($(du -m "$PRODUCT" | cut -f1) MiB)"
cat <<'NOTE'

   The disk image holds one item: Installer. Opening it offers the
   application, the machine, and the settings separately, and can take them
   away again. A machine that is already there is never replaced by default.
NOTE
