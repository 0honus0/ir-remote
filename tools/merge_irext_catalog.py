#!/usr/bin/env python3
"""Create an ESP8266 full-flash image with the IRext raw catalog payload."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_CATALOG_OFFSET = 0x300000


def merge_image(firmware: Path, catalog: Path, output: Path, offset: int) -> None:
    firmware_data = firmware.read_bytes()
    catalog_bytes = catalog.read_bytes()
    if len(firmware_data) > offset:
        raise RuntimeError(
            f"{firmware} is {len(firmware_data)} bytes, beyond catalog offset 0x{offset:X}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as handle:
        handle.write(firmware_data)
        handle.write(b"\xFF" * (offset - len(firmware_data)))
        handle.write(catalog_bytes)


def register_extra_flash_image(project_dir: Path, catalog: Path, offset: int) -> None:
    idedata = project_dir / ".pioenvs" / "ir-remote" / "idedata.json"
    if not idedata.exists():
        return
    data = json.loads(idedata.read_text(encoding="utf-8"))
    extra = data.setdefault("extra", {})
    images = extra.setdefault("flash_images", [])
    entry = {"path": str(catalog), "offset": f"0x{offset:X}"}
    images[:] = [
        image
        for image in images
        if not (
            image.get("offset") == entry["offset"]
            or Path(image.get("path", "")).name == catalog.name
        )
    ]
    images.append(entry)
    idedata.write_text(json.dumps(data), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--offset", type=lambda value: int(value, 0), default=DEFAULT_CATALOG_OFFSET)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    output = args.output
    if output is None:
        output = args.firmware.with_name(f"{args.firmware.stem}.with_irext.bin")
    merge_image(args.firmware, args.catalog, output, args.offset)
    print(
        f"IRext full image: {output} "
        f"(firmware={args.firmware.stat().st_size}, catalog={args.catalog.stat().st_size}, "
        f"offset=0x{args.offset:X})"
    )


if "Import" in globals():
    Import("env")  # type: ignore[name-defined]

    def _platformio_after_build(source, target, env):  # type: ignore[no-untyped-def]
        project_dir = Path(env["PROJECT_DIR"]).resolve()
        repo_root = project_dir.parents[3]
        firmware = Path(str(target[0])).resolve()
        catalog = repo_root / "generated" / "irext" / "catalog.bin"
        output = firmware.with_name(f"{firmware.stem}.with_irext.bin")
        merge_image(firmware, catalog, output, DEFAULT_CATALOG_OFFSET)
        register_extra_flash_image(project_dir, catalog, DEFAULT_CATALOG_OFFSET)
        print(
            f"IRext full image: {output} "
            f"(catalog offset=0x{DEFAULT_CATALOG_OFFSET:X}, catalog={catalog.stat().st_size})"
        )

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _platformio_after_build)  # type: ignore[name-defined]


if __name__ == "__main__":
    main()
