#!/bin/bash
# Build a Prose installer: the application with a machine inside it, signed,
# notarized and stapled, so it opens on a double click with nothing to dismiss.
#
#   scripts/make-installer.sh              # a .pkg that installs itself
#   scripts/make-installer.sh --dmg        # a disk image to drag across
#   scripts/make-installer.sh --no-notarize
#
# What it makes:
#
#   /Applications/Prose.app          the application, with prose.image inside it
#   ~/Library/Application Support/Prose/Machines/Prose.image
#                                    the machine, copied out on the first run
#   ~/Documents/HostFS               the folder shared into the machine
#
# The machine travels inside the bundle and is copied to the user's own folder
# the first time Prose runs, so the installer needs no per-user script, a
# reinstall never touches a machine someone has been using, and the same
# application works whether it arrived in a .pkg or a .dmg.
#
# Signing wants two certificates from the same Apple developer account:
#   Developer ID Application   signs the app and a .dmg
#   Developer ID Installer     signs a .pkg
# and notarizing wants a keychain profile, made once with
#   xcrun notarytool store-credentials macvm --apple-id … --team-id … --password …
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/installer"
APP="$ROOT/build/Prose.app"
IMAGE="${PROSE_IMAGE:-/Volumes/HaikuSrc/haiku/haiku-mmc.image}"
IDENTIFIER="org.prose.vm"
PROFILE="${PROSE_NOTARY_PROFILE:-macvm}"
VERSION="${PROSE_VERSION:-$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)}"

FORMAT=pkg
NOTARIZE=1
for arg in "$@"; do
	case "$arg" in
		--dmg) FORMAT=dmg ;;
		--pkg) FORMAT=pkg ;;
		--no-notarize) NOTARIZE=0 ;;
		*) echo "unknown option $arg" >&2; exit 64 ;;
	esac
done

say() { printf '\n== %s\n' "$*"; }
die() { printf '\n!! %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- identities
APP_ID="${PROSE_SIGN_APP:-$(security find-identity -v -p codesigning 2>/dev/null \
	| sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)}"
PKG_ID="${PROSE_SIGN_PKG:-$(security find-identity -v 2>/dev/null \
	| sed -n 's/.*"\(Developer ID Installer: .*\)"/\1/p' | head -1)}"

[ -n "$APP_ID" ] || die "no Developer ID Application certificate in the keychain.
   Xcode > Settings > Accounts > Manage Certificates > + > Developer ID Application."
if [ "$FORMAT" = pkg ] && [ -z "$PKG_ID" ]; then
	die "no Developer ID Installer certificate in the keychain, and a .pkg cannot be
   signed -- or notarized -- without one. Get it from
   developer.apple.com > Certificates, Identifiers & Profiles > + > Developer ID Installer,
   or build a disk image instead:  scripts/make-installer.sh --dmg"
fi

# ------------------------------------------------------------------- build
say "building the application"
"$ROOT/tools/build.sh"
[ -d "$APP" ] || die "no $APP"
[ -f "$IMAGE" ] || die "no machine at $IMAGE (set PROSE_IMAGE)"

rm -rf "$OUT"
mkdir -p "$OUT/root/Applications"
cp -R "$APP" "$OUT/root/Applications/Prose.app"
STAGED="$OUT/root/Applications/Prose.app"

say "putting the machine inside the application ($(du -m "$IMAGE" | cut -f1) MiB)"
cp "$IMAGE" "$STAGED/Contents/Resources/prose.image"

# the version the installer and the Finder show
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString 1.0" \
	-c "Set :CFBundleVersion $VERSION" "$STAGED/Contents/Info.plist" >/dev/null

# ------------------------------------------------------------------- sign
say "signing the application as “$APP_ID”"
# Hardened runtime is what notarization requires; the virtualization entitlement
# works inside it. Sign inside out -- here there is only the one executable.
codesign --force --timestamp --options runtime \
	--entitlements "$ROOT/tools/common/vz.entitlements" \
	--sign "$APP_ID" "$STAGED"
codesign --verify --deep --strict --verbose=1 "$STAGED"

# --------------------------------------------------- notarize the app itself
# Stapling the .dmg or .pkg staples the container, not the application inside
# it. Once someone drags Prose.app out, the container is gone and the
# application has no ticket of its own: Gatekeeper then has to ask Apple, and
# on a Mac that is offline -- or behind something that blocks it -- an
# application that passed here can be refused there. So the application is
# notarized first, on its own, and carries its ticket wherever it is copied.
if [ "$NOTARIZE" = 1 ]; then
	say "notarizing the application (so it keeps its ticket when copied out)"
	APPZIP="$OUT/Prose.app.zip"
	ditto -c -k --keepParent "$STAGED" "$APPZIP"
	xcrun notarytool submit "$APPZIP" --keychain-profile "$PROFILE" --wait \
		|| die "notarizing the application failed. For the reasons:
   xcrun notarytool log <submission-id> --keychain-profile $PROFILE"
	xcrun stapler staple "$STAGED"
	xcrun stapler validate "$STAGED"
	rm -f "$APPZIP"
fi

# ---------------------------------------------------------------- package
if [ "$FORMAT" = dmg ]; then
	PRODUCT="$OUT/Prose-$VERSION.dmg"
	say "building $PRODUCT"
	STAGE_DMG="$OUT/dmg"
	mkdir -p "$STAGE_DMG"
	cp -R "$STAGED" "$STAGE_DMG/Prose.app"
	ln -s /Applications "$STAGE_DMG/Applications"
	hdiutil create -quiet -ov -format UDZO -srcfolder "$STAGE_DMG" -volname Prose "$PRODUCT"
	codesign --force --timestamp --sign "$APP_ID" "$PRODUCT"
else
	PRODUCT="$OUT/Prose-$VERSION.pkg"
	say "building $PRODUCT"
	mkdir -p "$OUT/scripts"
	cat > "$OUT/scripts/postinstall" <<'POST'
#!/bin/bash
# The machine itself is copied out of the application on its first run, by the
# application, as the person who runs it -- an installer running as root must not
# create files in somebody's home folder. This only makes the shared folder, and
# only if it is missing.
set -e
HOME_DIR=$(dscl . -read "/Users/${USER}" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
[ -n "$HOME_DIR" ] || HOME_DIR="/Users/${USER}"
SHARE="$HOME_DIR/Documents/HostFS"
if [ ! -d "$SHARE" ]; then
	mkdir -p "$SHARE"
	chown "$USER" "$SHARE" 2>/dev/null || true
fi
exit 0
POST
	chmod +x "$OUT/scripts/postinstall"

	pkgbuild --quiet --root "$OUT/root" --identifier "$IDENTIFIER" --version "$VERSION" \
		--scripts "$OUT/scripts" --install-location / "$OUT/component.pkg"

	cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Prose</title>
    <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
    <volume-check>
        <allowed-os-versions><os-version min="27.0"/></allowed-os-versions>
    </volume-check>
    <pkg-ref id="$IDENTIFIER"/>
    <choices-outline><line choice="default"><line choice="$IDENTIFIER"/></line></choices-outline>
    <choice id="default"/>
    <choice id="$IDENTIFIER" visible="false"><pkg-ref id="$IDENTIFIER"/></choice>
    <pkg-ref id="$IDENTIFIER" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
XML

	productbuild --quiet --distribution "$OUT/distribution.xml" --package-path "$OUT" \
		"$OUT/unsigned.pkg"
	say "signing the installer as “$PKG_ID”"
	productsign --sign "$PKG_ID" "$OUT/unsigned.pkg" "$PRODUCT"
	rm -f "$OUT/unsigned.pkg" "$OUT/component.pkg"
fi

# --------------------------------------------------------------- notarize
if [ "$NOTARIZE" = 1 ]; then
	say "notarizing the $FORMAT with the “$PROFILE” profile (this waits for Apple)"
	xcrun notarytool submit "$PRODUCT" --keychain-profile "$PROFILE" --wait \
		|| die "notarization failed. For the reasons:
   xcrun notarytool log <submission-id> --keychain-profile $PROFILE"
	say "stapling"
	xcrun stapler staple "$PRODUCT"
	xcrun stapler validate "$PRODUCT"
	say "what Gatekeeper makes of it"
	spctl --assess --type "$([ "$FORMAT" = pkg ] && echo install || echo open)" \
		--context context:primary-signature -vv "$PRODUCT" 2>&1 | sed 's/^/   /'
else
	say "not notarized (--no-notarize): Gatekeeper will refuse this on another Mac"
fi

say "done: $PRODUCT  ($(du -m "$PRODUCT" | cut -f1) MiB)"
cat <<'NOTE'

   On its first run Prose makes, as the person running it:
     ~/Library/Application Support/Prose/Machines/Prose.image   the machine
     ~/Documents/HostFS                                         shared into it
NOTE
