# Shared, portable toolchain discovery for the _check scripts.
#
#   Visual Studio / SDK : found automatically (vswhere + Windows Kits)
#   wxWidgets           : the WXWIN environment variable, or a machine-local
#                         _check/env.local.sh override (never committed)
#
# After `source env.sh` a script can use, all as Windows-style paths:
#   BIN MSVCINC MSVCLIB  - the MSVC compiler / linker and its directories
#   KIT SDKV RC          - the Windows SDK (rc.exe included)
#   WX                   - the wxWidgets 3.2.2.1 tree
#   SRC CHK OUT          - Project1/, _check/ and _check/obj of THIS clone
#
# A machine may override anything by exporting it BEFORE sourcing, or by
# putting assignments in _check/env.local.sh (see .gitignore).

_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$_ENV_DIR/env.local.sh" ] && . "$_ENV_DIR/env.local.sh"

_w() { cygpath -w "$1" 2>/dev/null || echo "$1"; }

CHK="$(_w "$_ENV_DIR")"
SRC="$(_w "$_ENV_DIR/../Project1")"
OUT="$CHK\\obj"

# ---------------------------------------------------------------- wxWidgets
if [ -z "${WX:-}" ] && [ -n "${WXWIN:-}" ]; then
    WX="$(_w "$WXWIN")"
fi
_WXU="$(cygpath -u "${WX:-}" 2>/dev/null || echo "${WX:-}")"
if [ -z "$WX" ] || [ ! -d "$_WXU/include" ] || [ ! -d "$_WXU/lib" ]; then
    echo "env.sh: wxWidgets 3.2.2.1 not found." >&2
    echo "  Set WXWIN to your wxWidgets tree, e.g.:" >&2
    echo "      export WXWIN='C:\\wxWidgets-3.2.2.1'" >&2
    echo "  (or create _check/env.local.sh with WX=... -- see README.md)" >&2
    unset _ENV_DIR _WXU
    return 1 2>/dev/null || exit 1
fi

# -------------------------------------------------------- Visual Studio MSVC
if [ -z "${VS:-}" ]; then
    _VSW='/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
    _VSINST=''
    if [ -x "$_VSW" ]; then
        _VSINST="$("$_VSW" -latest -products '*' \
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
            -property installationPath 2>/dev/null | tr -d '\r')"
    fi
    if [ -n "$_VSINST" ]; then
        _MSVC="$(cygpath -u "$_VSINST")/VC/Tools/MSVC"
        _VER="$(ls "$_MSVC" 2>/dev/null | sort -V | tail -1)"
        [ -n "$_VER" ] && VS="$(_w "$_MSVC/$_VER")"
    fi
fi
if [ -z "${VS:-}" ]; then
    echo "env.sh: Visual Studio with the C++ toolchain was not found" >&2
    echo "  (vswhere came up empty). Install VS 2022+ with 'Desktop development" >&2
    echo "   with C++', or set VS=... in _check/env.local.sh." >&2
    unset _ENV_DIR _WXU
    return 1 2>/dev/null || exit 1
fi
BIN="$VS\\bin\\Hostx64\\x64"
MSVCINC="$VS\\include"
MSVCLIB="$VS\\lib\\x64"

# ------------------------------------------------------------- Windows SDK
if [ -z "${KIT:-}" ]; then
    KIT='C:\Program Files (x86)\Windows Kits\10'
fi
if [ -z "${SDKV:-}" ]; then
    _KITS="$(cygpath -u "$KIT" 2>/dev/null || echo "$KIT")/Include"
    if [ -d "$_KITS" ]; then
        SDKV="$(ls "$_KITS" 2>/dev/null | grep -E '^10\.' | sort -V | tail -1)"
    fi
fi
if [ -z "${SDKV:-}" ]; then
    echo "env.sh: no Windows 10 SDK under $KIT." >&2
    echo "  Install one from the Visual Studio installer, or set SDKV=... in" >&2
    echo "  _check/env.local.sh." >&2
    unset _ENV_DIR _WXU
    return 1 2>/dev/null || exit 1
fi
RC="$KIT\\bin\\${SDKV}\\x64\\rc.exe"

unset _ENV_DIR _WXU
