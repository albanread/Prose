#!/bin/bash
# Put ProseWriter into the Deskbar's Applications tree under ProseApps.
# One-time per image; the symlink survives binary deploys (the target file
# is replaced in place). When ProseWriter becomes a package, this is
# superseded by an entry in the tree's build/jam/DeskbarCategories
# (patches 0053/0055 put packaged apps into folders automatically).
set -euo pipefail
VM="$(cd "$(dirname "$0")" && pwd)"
"$VM/guest.sh" run 'mkdir -p "/boot/home/config/settings/deskbar/menu/Applications/ProseApps" && ln -sf /boot/home/apps/ProseWriter "/boot/home/config/settings/deskbar/menu/Applications/ProseApps/ProseWriter" && ls -l "/boot/home/config/settings/deskbar/menu/Applications/ProseApps/"' | tail -1
