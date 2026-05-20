#!/bin/bash
# Script para subir el Wayland compositor a tu fork de GitHub
# Ejecutar en la sesión de Termux donde tenés 'gh' logueado

set -e

cd "$(dirname "$0")"

REPO_DIR="$(pwd)"
REPO_NAME="termux-x11"

# Detectar usuario de GitHub via gh
GH_USER=$(gh api user -q '.login' 2>/dev/null)
if [ -z "$GH_USER" ]; then
    echo "❌ No se pudo detectar usuario de GitHub. ¿Estás logueado en gh?"
    echo "   Corré: gh auth login"
    exit 1
fi

echo "👤 Usuario GitHub detectado: $GH_USER"

# Verificar si ya tenés fork
FORK_EXISTS=$(gh repo view "$GH_USER/$REPO_NAME" --json name 2>/dev/null || echo "")

if [ -z "$FORK_EXISTS" ]; then
    echo "🍴 Creando fork de termux/termux-x11..."
    gh repo fork termux/termux-x11 --clone=false
    echo "✅ Fork creado: https://github.com/$GH_USER/$REPO_NAME"
else
    echo "✅ Fork ya existe: https://github.com/$GH_USER/$REPO_NAME"
fi

# Configurar git (si no está configurado)
if [ -z "$(git config user.name 2>/dev/null)" ]; then
    git config user.name "Wayland Developer"
fi
if [ -z "$(git config user.email 2>/dev/null)" ]; then
    git config user.email "wayland@termux.dev"
fi

# Agregar remote del fork
if ! git remote | grep -q "mi-fork"; then
    git remote add mi-fork "https://github.com/$GH_USER/$REPO_NAME.git"
    echo "🔗 Remote 'mi-fork' agregado"
fi

# Crear branch para el trabajo de Wayland
BRANCH_NAME="wayland-compositor-rewrite"
if git branch --list | grep -q "$BRANCH_NAME"; then
    git checkout "$BRANCH_NAME"
else
    git checkout -b "$BRANCH_NAME"
fi

# Stage de TODO (incluyendo archivos untracked)
echo "📦 Agregando archivos..."
git add -A

# Verificar si hay cambios para commitear
if git diff --cached --quiet; then
    echo "⚠️ No hay cambios para commitear"
    exit 0
fi

# Commit
echo "💾 Haciendo commit..."
git commit -m "feat: Wayland compositor rewrite with TDD

Complete Wayland compositor implementation for termux-x11:
- Build system: CMake recipes for wayland + wayland-protocols
- Core compositor: wl_display, wl_compositor, wl_output, wl_shm
- Surface management: wl_surface, wl_region, subcompositor
- GLES2/EGL renderer: multi-surface compositing with thread safety
- Input + Seat: wl_seat with pointer, keyboard, touch
- Protocols: xdg-shell, linux-dmabuf, wl_data_device_manager
- XWayland: launch and socket management
- Java/JNI: WaylandActivity, LorieWaylandView, dynamic registration
- Integration: main.c wiring everything together
- Tests: 53 tests in 11 suites with custom C framework
- SHM→Texture pipeline: wl_shm_buffer → LorieBuffer → GLES2

Key fixes from adversarial review:
- Single wl_seat global (not two)
- Proper EGL mutex separation from surfaces lock
- Buffer release to client on commit
- No VLA clipboard (bounded calloc)
- Keycode bounds checking
- Dynamic JNI registration (no JNI_OnLoad conflict)

All 10 PRs under 400-line budget each.
Total: ~3,800 lines (vs 8,372 original broken implementation)."

# Push
echo "🚀 Haciendo push a mi-fork..."
git push -u mi-fork "$BRANCH_NAME"

echo ""
echo "✅ ¡Listo! Tu trabajo está en:"
echo "   https://github.com/$GH_USER/$REPO_NAME/tree/$BRANCH_NAME"
echo ""
echo "📥 En tu PC de escritorio:"
echo "   git clone https://github.com/$GH_USER/$REPO_NAME.git"
echo "   cd termux-x11"
echo "   git checkout $BRANCH_NAME"
echo "   git submodule update --init --recursive"
