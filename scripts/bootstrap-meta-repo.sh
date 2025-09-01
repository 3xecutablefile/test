#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/bootstrap-meta-repo.sh /path/to/meta [KERNEL_REPO] [HOST_REPO]
# Defaults:
#   KERNEL_REPO=https://github.com/stable-rt/linux-stable.git
#   HOST_REPO=https://github.com/3xecutablefile/test.git

META_DIR=${1:-}
KERNEL_REPO=${2:-https://github.com/stable-rt/linux-stable.git}
HOST_REPO=${3:-https://github.com/3xecutablefile/test.git}

if [[ -z "${META_DIR}" ]]; then
  echo "usage: $0 /path/to/meta [KERNEL_REPO] [HOST_REPO]" >&2
  exit 1
fi

mkdir -p "${META_DIR}"
cd "${META_DIR}"

if [[ ! -d .git ]]; then
  git init
fi

mkdir -p kernels host scripts

if [[ ! -d kernels/linux ]]; then
  git submodule add "${KERNEL_REPO}" kernels/linux || true
fi

if [[ ! -d host/colinux2 ]]; then
  git submodule add "${HOST_REPO}" host/colinux2 || true
fi

git submodule update --init --recursive

cat > scripts/overlay-drivers-into-kernel.sh << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
# Copy guest driver sources into a kernel tree
# Usage: ./overlay-drivers-into-kernel.sh /path/to/kernel /path/to/colinux2

KERNEL_DIR=${1:-}
HOST_DIR=${2:-}
if [[ -z "${KERNEL_DIR}" || -z "${HOST_DIR}" ]]; then
  echo "usage: $0 /path/to/kernel /path/to/colinux2" >&2
  exit 1
fi

SRC="${HOST_DIR}/linux/drivers/colinux"
DST="${KERNEL_DIR}/drivers/colinux"

if [[ ! -d "${SRC}" ]]; then
  echo "missing source: ${SRC}" >&2
  exit 1
fi

mkdir -p "${DST}"
rsync -a --delete "${SRC}/" "${DST}/"

echo "Overlay complete: ${SRC} -> ${DST}"
echo "Remember to enable drivers in Kconfig and rebuild the kernel."
EOF

chmod +x scripts/overlay-drivers-into-kernel.sh

echo "Meta repo bootstrapped at ${META_DIR}"
echo "- Kernel: ${KERNEL_REPO} -> kernels/linux"
echo "- Host:   ${HOST_REPO} -> host/colinux2"
echo "Next: run ./scripts/overlay-drivers-into-kernel.sh kernels/linux host/colinux2"

