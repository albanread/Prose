# Paths for the private_workspace scripts. Source it: `source private_workspace/env.sh`.
# There is one Haiku tree: /Volumes/HaikuSrc/haiku on the branch "prose" (upstream's
# hrev60122 + main's patches/haiku/, applied by scripts/apply-patches.sh), built by
# scripts/build-image.sh. The separate checkout under private_workspace/ is gone.
export PW_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PW_HAIKU="${PW_HAIKU:-/Volumes/HaikuSrc/haiku}"
export PW_IMAGE="${PW_IMAGE:-$PW_HAIKU/haiku-mmc.image}"
export PW_WORK="$PW_ROOT/private_workspace/work"
export BFS_SHELL="$PW_HAIKU/generated/objects/darwin/arm64/release/tools/bfs_shell/bfs_shell"
export HVGPU="$PW_ROOT/build/bin/hvgpu"
# jam lives in ~/bin; brew bison/gettext are keg-only and must precede the system ones.
export PATH="$HOME/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/gettext/bin:$PATH"
