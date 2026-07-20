#!/usr/bin/env bash
set -euo pipefail

port=""
baud="460800"
config="configs/ir.yaml"
database=""
binary_dir="data/irext/bin"
output_dir="generated/irext"
catalog_offset="0x300000"
no_flash=0
erase_flash=0

usage() {
  cat <<'EOF'
Usage:
  tools/build_flash.sh --port /dev/ttyUSB0 [options]
  tools/build_flash.sh --no-flash [options]

Options:
  --port PORT          Serial port, for example COM3 or /dev/ttyUSB0.
  --baud BAUD          Upload baud rate. Default: 460800.
  --config FILE        ESPHome config. Default: configs/ir.yaml.
  --database FILE      IRext sqlite database. Default: auto-detect/extract.
  --binary-dir DIR     IRext bin directory. Default: data/irext/bin.
  --output-dir DIR     Generated catalog output directory. Default: generated/irext.
  --catalog-offset HEX Raw flash catalog offset. Default: 0x300000.
  --erase-flash        Erase flash before writing.
  --no-flash           Generate, compile, merge and verify only.
  -h, --help           Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      port="${2:?missing --port value}"
      shift 2
      ;;
    --baud)
      baud="${2:?missing --baud value}"
      shift 2
      ;;
    --config)
      config="${2:?missing --config value}"
      shift 2
      ;;
    --database)
      database="${2:?missing --database value}"
      shift 2
      ;;
    --binary-dir)
      binary_dir="${2:?missing --binary-dir value}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?missing --output-dir value}"
      shift 2
      ;;
    --catalog-offset)
      catalog_offset="${2:?missing --catalog-offset value}"
      shift 2
      ;;
    --erase-flash)
      erase_flash=1
      shift
      ;;
    --no-flash)
      no_flash=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

python_cmd="${PYTHON:-python}"

step() {
  echo
  echo "==> $*"
}

abs_path() {
  "$python_cmd" - "$1" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).resolve())
PY
}

find_database() {
  if [[ -n "$database" ]]; then
    if [[ ! -f "$database" ]]; then
      echo "Database not found: $database" >&2
      exit 1
    fi
    abs_path "$database"
    return
  fi

  local db_dir="data/irext/db"
  local preferred_db="$db_dir/irext_db_20260519_sqlite3.db"
  local zip_path="$db_dir/irext_db_20260519_sqlite3.zip"

  if [[ -s "$preferred_db" ]]; then
    abs_path "$preferred_db"
    return
  fi

  if [[ ! -f "$zip_path" ]]; then
    echo "No usable IRext database found under $db_dir" >&2
    exit 1
  fi

  step "Extract database"
  "$python_cmd" - "$zip_path" "$db_dir" <<'PY'
from pathlib import Path
import sys
import zipfile

zip_path = Path(sys.argv[1])
db_dir = Path(sys.argv[2])
with zipfile.ZipFile(zip_path) as archive:
    entry = next((item for item in archive.infolist() if item.filename.endswith(".db")), None)
    if entry is None:
        raise SystemExit(f"No .db file found in {zip_path}")
    target = db_dir / Path(entry.filename).name
    with archive.open(entry) as source, target.open("wb") as output:
        output.write(source.read())
    print(target.resolve())
PY
}

if [[ "$no_flash" -eq 0 && -z "$port" ]]; then
  echo "Port is required for flashing. Example: tools/build_flash.sh --port /dev/ttyUSB0" >&2
  exit 2
fi

if [[ ! -f "$config" ]]; then
  echo "Config not found: $config" >&2
  exit 1
fi
if [[ ! -d "$binary_dir" ]]; then
  echo "Binary dir not found: $binary_dir" >&2
  exit 1
fi

database_path="$(find_database | tail -n 1)"
catalog_path="$output_dir/catalog.bin"
index_path="$output_dir/catalog_index.h"
old_catalog_header="$output_dir/catalog_data.h"

step "Generate IRext catalog index and bin"
"$python_cmd" tools/export_irext_catalog.py "$database_path" \
  --binary-dir "$binary_dir" \
  --output-dir "$output_dir" \
  --emit-header \
  --allow-missing \
  --raw-flash-offset "$catalog_offset"

rm -f "$old_catalog_header"

if [[ ! -f "$catalog_path" || ! -f "$index_path" ]]; then
  echo "Catalog generation did not produce catalog.bin and catalog_index.h" >&2
  exit 1
fi

step "Compile ESPHome firmware"
"$python_cmd" -m esphome compile "$config"

build_root="$(dirname "$config")/.esphome/build"
firmware_path="$("$python_cmd" - "$build_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
files = sorted(root.rglob("firmware.bin"), key=lambda path: path.stat().st_mtime, reverse=True)
if not files:
    raise SystemExit(f"Build output not found: firmware.bin under {root}")
print(files[0].resolve())
PY
)"
full_image_path="$(dirname "$firmware_path")/firmware.with_irext.bin"

step "Merge firmware and IRext catalog"
"$python_cmd" tools/merge_irext_catalog.py "$firmware_path" "$catalog_path" \
  --offset "$catalog_offset" \
  --output "$full_image_path"

step "Verify merged image"
"$python_cmd" - "$firmware_path" "$catalog_path" "$full_image_path" "$catalog_offset" <<'PY'
from pathlib import Path
import sys

firmware = Path(sys.argv[1]).read_bytes()
catalog = Path(sys.argv[2]).read_bytes()
full = Path(sys.argv[3]).read_bytes()
offset = int(sys.argv[4], 0)

if not full or full[0] != 0xE9:
    raise SystemExit("full image does not start with ESP8266 image header 0xE9")
if full[:len(firmware)] != firmware:
    raise SystemExit("firmware prefix mismatch")
if full[offset:offset + len(catalog)] != catalog:
    raise SystemExit("catalog payload mismatch at offset")
print(f"verified: firmware={len(firmware)} full={len(full)} catalog={len(catalog)} offset=0x{offset:X}")
PY

if [[ "$no_flash" -eq 1 ]]; then
  echo
  echo "NoFlash enabled. Skip flashing."
  echo "Full image: $full_image_path"
  exit 0
fi

if [[ "$erase_flash" -eq 1 ]]; then
  step "Erase flash"
  "$python_cmd" -m esptool --chip esp8266 --port "$port" --baud "$baud" erase-flash
fi

step "Flash full image"
"$python_cmd" -m esptool \
  --chip esp8266 \
  --port "$port" \
  --baud "$baud" \
  write-flash \
  -z \
  --flash-mode dout \
  --flash-freq 40m \
  --flash-size 4MB \
  0x0 "$full_image_path"

echo
echo "Done."
