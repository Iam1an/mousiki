#!/usr/bin/env bash

set -e

OS="$(uname -s)"

# -------------------------------
# Helpers
# -------------------------------

check_command() {
    command -v "$1" >/dev/null 2>&1
}

print_dep() {
    local name="$1"
    local command="$2"

    if check_command "$command"; then
        printf "%-14s ✓\n" "$name"
        return 0
    else
        printf "%-14s ✗\n" "$name"
        return 1
    fi
}

echo ""
echo "========================================="
echo "          Mousiki Setup"
echo "========================================="
echo ""

echo "OS detecting..."
echo "  $OS"
echo ""

# -------------------------------
# Detect platform
# -------------------------------

IS_TERMUX=false

if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux" ]; then
    IS_TERMUX=true
    echo "Platform: Termux"
elif [ "$OS" = "Darwin" ]; then
    echo "Platform: macOS"
elif [ "$OS" = "Linux" ]; then
    echo "Platform: Linux"
else
    echo "Error: Unsupported operating system: $OS"
    exit 1
fi

echo ""

# -------------------------------
# Dependency checking
# -------------------------------

echo "======== Checking deps =========="

MISSING=()

if ! print_dep "ffmpeg" "ffmpeg"; then
    MISSING+=("ffmpeg")
fi

if [ "$IS_TERMUX" = true ]; then
    if ! print_dep "clang" "clang"; then
        MISSING+=("clang")
    fi

    if ! print_dep "make" "make"; then
        MISSING+=("make")
    fi

    if ! print_dep "python" "python"; then
        MISSING+=("python")
    fi

    PYTHON_CMD="python"

else
    if ! print_dep "make" "make"; then
        MISSING+=("make")
    fi

    if ! print_dep "python" "python3"; then
        MISSING+=("python3")
    fi

    PYTHON_CMD="python3"
fi

if ! print_dep "yt-dlp" "yt-dlp"; then
    MISSING+=("yt-dlp")
fi

if ! print_dep "cmake" "cmake"; then
    MISSING+=("cmake")
fi

# -------------------------------
# Python environment
# -------------------------------
# requests (used by the lyrics fetcher) is a SOFT dependency: mousiki
# must still build and run without it, regardless of what Python
# environment (or lack thereof) the user has. Nothing in this section is
# allowed to exit the script.

echo "======== Checking Python environment =========="

PIP_CMD="$(command -v pip 2>/dev/null || true)"
PIP3_CMD="$(command -v pip3 2>/dev/null || true)"
PIPX_CMD="$(command -v pipx 2>/dev/null || true)"

PY_MANAGERS=()

if [ -n "$PIPX_CMD" ]; then
    printf "%-14s ✓  (%s)\n" "pipx" "$PIPX_CMD"
    PY_MANAGERS+=("pipx")
else
    printf "%-14s ✗\n" "pipx"
fi

if [ -n "$PIP3_CMD" ]; then
    printf "%-14s ✓  (%s)\n" "pip3" "$PIP3_CMD"
    PY_MANAGERS+=("pip3")
else
    printf "%-14s ✗\n" "pip3"
fi

if [ -n "$PIP_CMD" ]; then
    printf "%-14s ✓  (%s)\n" "pip" "$PIP_CMD"
    PY_MANAGERS+=("pip")
else
    printf "%-14s ✗\n" "pip"
fi

echo ""

# -------------------------------
# requests (soft dependency)
# -------------------------------
# The lyrics fetcher (scripts/lrc.py) needs Python's `requests` package to
# talk to Better Lyrics/LRCLIB. This used to be `syncedlyrics`; the actual
# package changed but the soft-dependency contract hasn't: mousiki must
# still build and run fine without it, lyrics just won't be available.

REQUESTS_OK=false

requests_installed() {
    # Covers pip/pip3 installs (regular or --user) into the active interpreter.
    if "$PYTHON_CMD" -c "import requests" >/dev/null 2>&1; then
        return 0
    fi

    # Covers pipx's isolated venv installs. `pipx list --short` prints a
    # predictable "<n> <version>" line per app, unlike the indented
    # human-readable `pipx list`, which is not safe to grep by anchor.
    if [ -n "$PIPX_CMD" ] && "$PIPX_CMD" list --short 2>/dev/null | grep -q '^requests '; then
        return 0
    fi

    return 1
}

install_requests() {
    local mgr

    for mgr in "${PY_MANAGERS[@]}"; do
        echo "==> Trying to install requests with $mgr..."

        case "$mgr" in
            pipx)
                # pipx normally installs CLI *applications* into their own
                # isolated venv, not libraries -- but `pipx install
                # requests` still works (it just isolates the package),
                # and scripts/fetch_lyrics.py knows how to find that venv's
                # site-packages at runtime and splice it onto sys.path (see
                # _find_pipx_site_packages() there), the same way it would
                # for any other pipx-isolated package.
                if "$PIPX_CMD" install requests >/dev/null 2>&1; then
                    echo "Installed requests with pipx."
                    return 0
                fi
                ;;
            pip3)
                if "$PIP3_CMD" install --user requests >/dev/null 2>&1; then
                    echo "Installed requests with pip3."
                    return 0
                fi
                ;;
            pip)
                if "$PIP_CMD" install --user requests >/dev/null 2>&1; then
                    echo "Installed requests with pip."
                    return 0
                fi
                ;;
        esac

        echo "    $mgr failed or is already in a weird state, trying next option..."
    done

    return 1
}

if requests_installed; then
    printf "%-14s ✓\n" "requests"
    REQUESTS_OK=true
else
    printf "%-14s ✗  (optional)\n" "requests"
    echo ""

    if [ ${#PY_MANAGERS[@]} -eq 0 ]; then
        echo "No Python package manager (pip, pip3, pipx) was found."
        echo "requests is optional, so setup will continue without it."
        echo "Install pip/pip3/pipx and re-run this script later to add it."
    else
        if install_requests; then
            if requests_installed; then
                REQUESTS_OK=true
            else
                echo ""
                echo "requests reported a successful install but could not"
                echo "be verified. Continuing anyway since it is optional."
            fi
        else
            echo ""
            echo "Could not install requests automatically."
            echo "This is optional -- mousiki will still build and run without it."
            echo "You can install it later yourself, e.g.:"
            [ -n "$PIPX_CMD" ] && echo "  pipx install requests"
            [ -n "$PIP3_CMD" ] && echo "  pip3 install --user requests"
            [ -n "$PIP_CMD" ] && echo "  pip install --user requests"
        fi
    fi
fi


# -------------------------------
# Install missing dependencies
# -------------------------------

if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    echo "Missing dependencies:"
    printf '  - %s\n' "${MISSING[@]}"
    echo ""

    if [ "$IS_TERMUX" = true ]; then
        echo "Termux dependencies are not installed automatically."
        echo "Please install the missing packages and run setup.sh again."
        exit 1
    fi

    if [ "$OS" = "Darwin" ]; then

        if ! check_command brew; then
            echo "Error: Homebrew is required."
            echo "Install Homebrew and run setup.sh again."
            exit 1
        fi

        echo "==> Installing missing macOS dependencies..."

        BREW_DEPS=()

        for dep in "${MISSING[@]}"; do
            case "$dep" in
                python3)
                    BREW_DEPS+=("python3")
                    ;;
                *)
                    BREW_DEPS+=("$dep")
                    ;;
            esac
        done

        HOMEBREW_NO_AUTO_UPDATE=1 brew install "${BREW_DEPS[@]}"

    elif [ "$OS" = "Linux" ]; then

        if check_command apt-get; then

            echo "==> Installing missing dependencies via apt..."

            sudo apt-get update
            sudo apt-get install -y \
                cmake \
                build-essential \
                ffmpeg \
                yt-dlp \
                python3 \
                python3-pip

        elif check_command pacman; then

            echo "==> Installing missing dependencies via pacman..."

            sudo pacman -Sy --noconfirm \
                cmake \
                base-devel \
                ffmpeg \
                yt-dlp \
                python \
                python-pip

        elif check_command dnf; then

            echo "==> Installing missing dependencies via dnf..."

            sudo dnf install -y \
                cmake \
                gcc-c++ \
                make \
                ffmpeg \
                yt-dlp \
                python3 \
                python3-pip

        else
            echo "Error: Unsupported Linux package manager."
            exit 1
        fi
    fi
fi

# -------------------------------
# Verify dependencies again
# -------------------------------

echo ""
echo "======== Verifying deps ========="

for cmd in cmake ffmpeg yt-dlp "$PYTHON_CMD"; do
    if ! check_command "$cmd"; then
        echo "Error: $cmd is still missing."
        exit 1
    fi
done

if [ "$IS_TERMUX" = true ] && ! check_command clang; then
    echo "Error: clang is still missing."
    exit 1
fi

if [ "$REQUESTS_OK" = true ]; then
    printf "%-14s ✓\n" "requests"
else
    printf "%-14s ✗  (optional, skipping)\n" "requests"
fi

echo ""
echo "All required dependencies are ready."
echo ""

# -------------------------------
# Configuration
# -------------------------------

echo "======== Configuring Mousiki ========"

CONFIG_DIR="$HOME/.config/mousiki"
mkdir -p "$CONFIG_DIR"

if [ ! -f "$CONFIG_DIR/config.txt" ]; then
    cp config.txt "$CONFIG_DIR/config.txt"
    echo "Created $CONFIG_DIR/config.txt"
else
    echo "Existing config found. Keeping current file."
fi

YTDLP_CONFIG_DIR="$HOME/.config/yt-dlp"
mkdir -p "$YTDLP_CONFIG_DIR"

if [ ! -f "$YTDLP_CONFIG_DIR/config" ]; then
    echo '--extractor-args "youtube:player_client=android"' \
        > "$YTDLP_CONFIG_DIR/config"

    echo "Configured yt-dlp."
elif ! grep -q "player_client" "$YTDLP_CONFIG_DIR/config"; then
    echo '--extractor-args "youtube:player_client=android"' \
        >> "$YTDLP_CONFIG_DIR/config"

    echo "Updated yt-dlp configuration."
else
    echo "yt-dlp configuration already exists."
fi

# -------------------------------
# Build
# -------------------------------

echo ""
echo "======== Building ( cmake ) ========"

cmake -B build

echo ""
echo "======== Building ( make ) ========="

CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

cmake --build build -j"$CORES"

# -------------------------------
# Verify build
# -------------------------------

BINARY="./build/mousiki"

if [ ! -f "$BINARY" ]; then
    echo ""
    echo "Error: Build completed but binary was not found."
    exit 1
fi

echo ""
echo "======== Build completed ==========="
echo ""
echo "Binary: $BINARY"
echo ""

# -------------------------------
# Optional binary installation
# -------------------------------

if [ "$IS_TERMUX" = true ]; then
    BIN_DIR="$PREFIX/bin"
else
    BIN_DIR="/usr/local/bin"
fi

printf "Want to copy binary to %s? (Y/N) " "$BIN_DIR"
read -r INSTALL_BINARY

if [[ "$INSTALL_BINARY" =~ ^[Yy]$ ]]; then

    mkdir -p "$BIN_DIR"

    if [ -w "$BIN_DIR" ]; then
        cp "$BINARY" "$BIN_DIR/mousiki"
    else
        sudo cp "$BINARY" "$BIN_DIR/mousiki"
    fi

    echo ""
    echo "copied!!"
    echo ""
    echo "Run Mousiki with:"
    echo "  mousiki"
else
    echo ""
    echo "Binary left at:"
    echo "  $BINARY"
fi

echo ""
echo "========================================="
echo "        Mousiki setup complete!"
echo "========================================="
