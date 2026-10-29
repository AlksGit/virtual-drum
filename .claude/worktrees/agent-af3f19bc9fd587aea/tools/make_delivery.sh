#!/usr/bin/env bash
# =============================================================================
# make_delivery.sh — build the client delivery archive for the Raspberry Pi
# (TASK-111). Produces a self-contained .zip holding everything that runs on
# the Pi, so the client never needs access to the git repository.
#
# Design:
#   - The archive is exported from GIT (not from the working tree). This is the
#     whole point: git stores text files with LF endings and keeps the 100755
#     mode bits, whereas the Windows working tree has CRLF (core.autocrlf=true)
#     which would make the shell scripts unrunnable on the Pi.
#   - The content is an EXPLICIT manifest (see MANIFEST below), never a blanket
#     copy: a wide copy would ship the desktop app, the Algo_detection
#     spreadsheets and the virtualenvs to the client.
#   - The App/ tree layout is PRESERVED, because provision_pi.sh resolves the
#     repo root from its own location (scripts/ -> web/ -> App/ -> root) and
#     expects <root>/App/requirements.txt. Flattening it would break it.
#   - fail-fast: any missing piece aborts with a clear message.
#
# Output: dist/Septodont-NoScan-software-deliverables-YYYY-MM-DD.zip
#         (dist/ is already git-ignored, so the archive is never committed.)
#
# Usage:
#     bash tools/make_delivery.sh
#
# Environment overrides:
#   NOSCAN_DELIVERY_REV   git revision to export (default: HEAD)
#   NOSCAN_DELIVERY_OUT   output directory     (default: <repo>/dist)
# =============================================================================

set -euo pipefail

# --- Configuration -----------------------------------------------------------
BUNDLE_NAME="Septodont-NoScan-software-deliverables"
REV="${NOSCAN_DELIVERY_REV:-HEAD}"
BUILD_DATE="$(date +%F)"   # resolved once: the archive name and VERSION.txt must agree

# --- Resolve repo layout from this script's own location ---------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"          # tools/ -> repo
OUT_DIR="${NOSCAN_DELIVERY_OUT:-${REPO_ROOT}/dist}"

# --- Delivery manifest -------------------------------------------------------
# Everything the Pi needs at runtime, and nothing else. Paths are relative to
# the repo root and must exist in the exported revision.
#
# Note: corridor_tracker.py is deliberately absent — the web UI recomputes the
# corridor in JavaScript, the server never imports it.
MANIFEST=(
    # -- shared detection code (imported by the web server) --
    "App/NOSCAN_app_detection_algo.py"
    "App/detection_config.json"
    "App/noscan-tooth.jpg"
    # -- python dependencies (3.11 = Bookworm, 3.13 = Trixie) --
    "App/requirements.txt"
    "App/requirements-linux.txt"
    # -- web server + UI --
    "App/web/noscan_server.py"
    "App/web/requirements-server.txt"
    "App/web/templates"
    "App/web/static"
    # -- system scripts (provisioning, service, kiosk, access point) --
    "App/web/scripts"
    # -- documentation (remapped into docs/ below) --
    "INSTALL_RPI.md"
    "App/docs/mode_emploi.md"
    "App/docs/user_manual.md"
    "tools/README_INSTALL_CLIENT.md"
)

# Content that must NEVER reach the client, verified on the produced archive.
FORBIDDEN=(
    ".git/" "noscan_env" "/venv/" ".pyc" "__pycache__" "Algo_detection"
    "3D_axis_config" "App/archive/" "App/backup/" "InfosFW" "NOSCAN_app_v2.py"
    "BACKLOG.md" "CLAUDE.md" "Logs/" "tmp_"
)

# --- Output helpers ----------------------------------------------------------
step()  { printf '\n\033[1;34m▶ %s\033[0m\n' "$*"; }
ok()    { printf '  \033[1;32m✓ %s\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }
warn()  { printf '  \033[1;33m! %s\033[0m\n' "$*"; }
# die "what failed" "how to fix" -> clear message + abort.
die() {
    printf '\n\033[1;31m✗ FAILED: %s\033[0m\n' "$1" >&2
    [ -n "${2:-}" ] && printf '  → fix: %s\n' "$2" >&2
    exit 1
}
# Same as die(), but deletes the archive first: a bundle that failed its checks
# must never be left on disk where it could be shipped by mistake.
die_bad_zip() {
    if [ -n "${ZIP_PATH:-}" ]; then rm -f "$ZIP_PATH"; fi
    die "$@"
}

# --- Staging area (always cleaned up, even on error) -------------------------
STAGE_ROOT=""
cleanup() { [ -n "$STAGE_ROOT" ] && [ -d "$STAGE_ROOT" ] && rm -rf "$STAGE_ROOT"; }
trap cleanup EXIT

# =============================================================================
# 0. Pre-flight checks
# =============================================================================
preflight() {
    step "0. Pre-flight checks"

    command -v git >/dev/null 2>&1 || die "git not found." "Install git; the archive is exported from the repository."
    command -v tar >/dev/null 2>&1 || die "tar not found." "Install tar (present in Git Bash and on Linux)."

    # Python does the zipping: it is the only portable way to force the 0755
    # mode bits into the archive from Windows, where the filesystem has none.
    PY_BIN=""
    for cand in python3 python; do
        if command -v "$cand" >/dev/null 2>&1; then PY_BIN="$cand"; break; fi
    done
    [ -n "$PY_BIN" ] || die "python3 not found." "Install Python 3; it is used to write the .zip."
    ok "tools: git, tar, ${PY_BIN}"

    git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1 \
        || die "Not a git repository: ${REPO_ROOT}." "Run this script from inside the repo clone."

    git -C "$REPO_ROOT" rev-parse --verify "${REV}^{commit}" >/dev/null 2>&1 \
        || die "Revision '${REV}' not found." "Use an existing commit/branch in NOSCAN_DELIVERY_REV."

    # Every manifest entry must exist in the exported revision.
    local missing=0 entry
    for entry in "${MANIFEST[@]}"; do
        if ! git -C "$REPO_ROOT" cat-file -e "${REV}:${entry}" 2>/dev/null; then
            warn "missing in ${REV}: ${entry}"
            missing=1
        fi
    done
    [ "$missing" -eq 0 ] || die "Manifest entries missing from revision '${REV}'." \
        "Commit them first — the archive is built from git, not from the working tree."
    ok "manifest complete (${#MANIFEST[@]} entries)"

    # The archive comes from the commit, so uncommitted edits are NOT included.
    local dirty
    dirty="$(git -C "$REPO_ROOT" status --porcelain -- "${MANIFEST[@]}" 2>/dev/null || true)"
    if [ -n "$dirty" ] && [ "$REV" = "HEAD" ]; then
        warn "uncommitted changes on delivery files — they will NOT be in the archive:"
        printf '%s\n' "$dirty" | sed 's/^/      /'
    fi
}

# =============================================================================
# 1. Export the manifest from git into a staging directory
# =============================================================================
export_files() {
    step "1. Export from git (${REV})"

    mkdir -p "$OUT_DIR" || die "Cannot create output directory ${OUT_DIR}." "Check permissions."
    STAGE_ROOT="$(mktemp -d "${OUT_DIR}/.delivery-stage-XXXXXX")" \
        || die "Cannot create staging directory." "Check permissions on ${OUT_DIR}."
    STAGE="${STAGE_ROOT}/${BUNDLE_NAME}"
    mkdir -p "$STAGE"

    # git archive applies the working-tree line-ending conversion, so on Windows
    # (core.autocrlf=true) it would emit CRLF and the Pi could not run the shell
    # scripts. Forcing autocrlf=false + eol=lf for this call gives the committed
    # LF content; binaries are untouched (marked `binary` in .gitattributes).
    git -C "$REPO_ROOT" -c core.autocrlf=false -c core.eol=lf \
        archive --format=tar "$REV" -- "${MANIFEST[@]}" \
        | tar -x -C "$STAGE" \
        || die "git archive/extract failed." "Check the manifest paths exist in ${REV}."
    ok "exported to staging"
}

# =============================================================================
# 2. Lay out the bundle (documentation regrouped, README at the root)
# =============================================================================
arrange_bundle() {
    step "2. Arrange bundle layout"

    mkdir -p "${STAGE}/docs"
    mv "${STAGE}/INSTALL_RPI.md"           "${STAGE}/docs/"
    mv "${STAGE}/App/docs/mode_emploi.md"  "${STAGE}/docs/"
    mv "${STAGE}/App/docs/user_manual.md"  "${STAGE}/docs/"
    rmdir "${STAGE}/App/docs" || die "App/docs still holds unexpected files." \
        "Only mode_emploi.md and user_manual.md should have been exported."

    mv "${STAGE}/tools/README_INSTALL_CLIENT.md" "${STAGE}/"
    rmdir "${STAGE}/tools" || die "tools/ still holds unexpected files." \
        "Only README_INSTALL_CLIENT.md should have been exported."

    ok "docs/ regrouped, README at bundle root"
}

# =============================================================================
# 3. Stamp the delivered version
# =============================================================================
write_version() {
    step "3. Write VERSION.txt"

    local commit
    commit="$(git -C "$REPO_ROOT" rev-parse --short "$REV")"

    cat > "${STAGE}/VERSION.txt" <<EOF
NoScan — Raspberry Pi software delivery
=======================================

Bundle    : ${BUNDLE_NAME}
Build date: ${BUILD_DATE}
Source    : commit ${commit} ($(git -C "$REPO_ROOT" log -1 --format=%cd --date=short "$REV"))

Installation: see README_INSTALL_CLIENT.md
User manual : docs/mode_emploi.md (FR) — docs/user_manual.md (EN)
EOF
    ok "VERSION.txt (commit ${commit}, built ${BUILD_DATE})"
}

# =============================================================================
# 4. Write the .zip (explicit unix modes: 0755 for scripts, 0644 otherwise)
# =============================================================================
write_zip() {
    step "4. Write archive"

    ZIP_PATH="${OUT_DIR}/${BUNDLE_NAME}-${BUILD_DATE}.zip"
    rm -f "$ZIP_PATH"

    "$PY_BIN" - "$STAGE_ROOT" "$BUNDLE_NAME" "$ZIP_PATH" "$BUILD_DATE" <<'PY' || die "Writing the archive failed." "See the Python error above."
import os, sys, zipfile

stage_root, bundle, zip_path, build_date = sys.argv[1:5]
root = os.path.join(stage_root, bundle)

# Every entry carries the build date rather than the staging file's mtime, so
# two builds of the same commit are byte-identical and the client sees a
# meaningful date instead of the 1980 zip epoch.
stamp = tuple(int(p) for p in build_date.split("-")) + (0, 0, 0)

# Windows has no executable bit, so it is set here from the file type instead
# of being read from the filesystem: the Pi needs 0755 on the shell scripts.
def mode_for(name):
    return 0o755 if name.endswith(".sh") else 0o644

paths = []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames.sort()
    for fn in sorted(filenames):
        paths.append(os.path.join(dirpath, fn))

with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for full in paths:
        arc = os.path.join(bundle, os.path.relpath(full, root)).replace(os.sep, "/")
        info = zipfile.ZipInfo(arc, date_time=stamp)
        info.external_attr = mode_for(arc) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        with open(full, "rb") as fh:
            zf.writestr(info, fh.read())

print(f"  wrote {len(paths)} files")
PY

    [ -f "$ZIP_PATH" ] || die "Archive not produced." "See the Python error above."
    ok "$(basename "$ZIP_PATH")"
}

# =============================================================================
# 5. Verify the produced archive
# =============================================================================
verify_zip() {
    step "5. Verify archive"

    local staged_count zip_count
    staged_count="$(find "$STAGE" -type f | wc -l | tr -d ' ')"
    zip_count="$("$PY_BIN" -c "import zipfile,sys; print(len(zipfile.ZipFile(sys.argv[1]).namelist()))" "$ZIP_PATH")"
    [ "$staged_count" -eq "$zip_count" ] \
        || die_bad_zip "Archive holds ${zip_count} files but ${staged_count} were staged." "Re-run; check for locked files."
    ok "${zip_count} files archived (matches staging)"

    # Nothing internal may have slipped in.
    local names pattern hits=0
    names="$("$PY_BIN" -c "import zipfile,sys; print('\n'.join(zipfile.ZipFile(sys.argv[1]).namelist()))" "$ZIP_PATH")"
    for pattern in "${FORBIDDEN[@]}"; do
        if printf '%s\n' "$names" | grep -qF -- "$pattern"; then
            warn "forbidden content matched '${pattern}':"
            printf '%s\n' "$names" | grep -F -- "$pattern" | sed 's/^/      /'
            hits=1
        fi
    done
    [ "$hits" -eq 0 ] || die_bad_zip "Internal content found in the archive." "Fix the MANIFEST in this script."
    ok "no internal/private content"

    # The scripts must be runnable, and no text file may carry CRLF: a CRLF
    # shell script aborts on the Pi with "$'\r': command not found".
    "$PY_BIN" - "$ZIP_PATH" <<'PY' || die_bad_zip "Mode / line-ending check failed." "See the failing files above."
import sys, zipfile

TEXT_SUFFIXES = (".sh", ".py", ".md", ".txt", ".json", ".html", ".css", ".js",
                 ".service", ".example")
bad = []
with zipfile.ZipFile(sys.argv[1]) as zf:
    for info in zf.infolist():
        name = info.filename
        if name.endswith(".sh") and (info.external_attr >> 16) & 0o111 == 0:
            bad.append(f"{name}: not executable ({info.external_attr >> 16:o})")
        if name.endswith(TEXT_SUFFIXES) and b"\r\n" in zf.read(name):
            bad.append(f"{name}: CRLF line endings (would break on the Pi)")

if bad:
    print("\n".join("      " + b for b in bad), file=sys.stderr)
    sys.exit(1)
PY
    ok "shell scripts executable, all text files LF"
}

# =============================================================================
# 6. Summary
# =============================================================================
summary() {
    local size
    size="$(du -h "$ZIP_PATH" | cut -f1)"

    step "Delivery archive ready ✓"
    info "File: ${ZIP_PATH}"
    info "Size: ${size}"
    info ""
    info "Next steps:"
    info "  • Copy the .zip onto the Raspberry Pi (USB stick, scp, ...)."
    info "  • The client follows README_INSTALL_CLIENT.md inside the archive."
    info ""
    info "The archive is built from commit $(git -C "$REPO_ROOT" rev-parse --short "$REV")."
}

preflight
export_files
arrange_bundle
write_version
write_zip
verify_zip
summary
