#!/usr/bin/env python3
"""Generate and validate multi-boot partition layouts.

The Xteink X4 has 16 MB of flash, so the number of firmware slots is a trade off
between slot size and slot count (see docs/limitations.md). This script writes
the CSV layouts consumed by PlatformIO and can validate any layout for overlaps,
alignment and flash overflow.

Examples
--------
    # regenerate the checked in layouts
    python3 tools/gen_partitions.py --write

    # validate every layout (also run in CI)
    python3 tools/gen_partitions.py --check partitions/*.csv

    # print a custom layout without writing it
    python3 tools/gen_partitions.py --slots 3 --slot-size 4M
"""

from __future__ import annotations

import argparse
import csv
import io
import os
import sys
from dataclasses import dataclass

FLASH_SIZE = 16 * 1024 * 1024
APP_ALIGNMENT = 0x10000
DATA_ALIGNMENT = 0x1000
FIRST_PARTITION_OFFSET = 0x9000

# Fixed head of every layout: NVS, otadata and the boot selector itself.
NVS_SIZE = 0x5000
OTADATA_SIZE = 0x2000
SELECTOR_SIZE = 0xF0000  # 960 KB, plenty for the selector app
MBMETA_SIZE = 0x1000  # slot names and boot preferences
COREDUMP_SIZE = 0x10000


@dataclass
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


def parse_size(value: str) -> int:
    """Parses sizes such as ``0x640000``, ``6M`` or ``512K``."""
    text = value.strip().upper()
    multiplier = 1
    if text.endswith("M"):
        multiplier, text = 1024 * 1024, text[:-1]
    elif text.endswith("K"):
        multiplier, text = 1024, text[:-1]
    base = 16 if text.startswith("0X") else 10
    return int(text, base) * multiplier


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def build_layout(slot_count: int, slot_size: int, spiffs_size: int | None = None) -> list[Partition]:
    """Builds a layout with `slot_count` app slots of `slot_size` bytes each."""
    if slot_count < 1 or slot_count > 15:
        raise ValueError("slot_count must be between 1 and 15 (ota_0..ota_14)")
    if slot_size % APP_ALIGNMENT:
        raise ValueError("slot_size must be a multiple of 64 KiB")

    partitions: list[Partition] = []
    offset = FIRST_PARTITION_OFFSET
    partitions.append(Partition("nvs", "data", "nvs", offset, NVS_SIZE))
    offset += NVS_SIZE
    partitions.append(Partition("otadata", "data", "ota", offset, OTADATA_SIZE))
    offset += OTADATA_SIZE

    offset = align_up(offset, APP_ALIGNMENT)
    partitions.append(Partition("selector", "app", "factory", offset, SELECTOR_SIZE))
    offset += SELECTOR_SIZE

    partitions.append(Partition("mbmeta", "data", "0x40", offset, MBMETA_SIZE))
    offset += MBMETA_SIZE

    coredump_offset = FLASH_SIZE - COREDUMP_SIZE
    slots_total = slot_count * slot_size
    first_slot_offset = align_up(offset, APP_ALIGNMENT)
    available_for_spiffs = coredump_offset - slots_total - first_slot_offset
    if available_for_spiffs < 0:
        raise ValueError(
            f"{slot_count} slots of {slot_size / 1024 / 1024:.2f} MB do not fit in 16 MB of flash"
        )
    if spiffs_size is None:
        # Everything that is left over becomes the shared SPIFFS partition that
        # guest firmwares expect to find.
        spiffs_size = (available_for_spiffs // DATA_ALIGNMENT) * DATA_ALIGNMENT
    elif spiffs_size > available_for_spiffs:
        raise ValueError("requested SPIFFS partition does not fit alongside the slots")

    if spiffs_size > 0:
        partitions.append(Partition("spiffs", "data", "spiffs", offset, spiffs_size))
        offset += spiffs_size

    offset = align_up(offset, APP_ALIGNMENT)
    for index in range(slot_count):
        partitions.append(Partition(f"slot{index}", "app", f"ota_{index}", offset, slot_size))
        offset += slot_size

    partitions.append(Partition("coredump", "data", "coredump", coredump_offset, COREDUMP_SIZE))
    validate(partitions)
    return partitions


def render(partitions: list[Partition], header_comment: str) -> str:
    out = io.StringIO()
    out.write(header_comment.rstrip() + "\n")
    out.write("# Name,     Type, SubType,  Offset,     Size,       Flags\n")
    for partition in partitions:
        line = (
            f"{partition.name + ',':<11}{partition.type + ',':<6}{partition.subtype + ',':<10}"
            f"{hex(partition.offset) + ',':<12}{hex(partition.size) + ','}"
        )
        out.write(line.rstrip() + "\n")
    return out.getvalue()


def parse_csv(text: str) -> list[Partition]:
    partitions: list[Partition] = []
    for row in csv.reader(io.StringIO(text)):
        if not row or row[0].strip().startswith("#") or not row[0].strip():
            continue
        name, type_, subtype, offset, size = (cell.strip() for cell in row[:5])
        partitions.append(Partition(name, type_, subtype, parse_size(offset), parse_size(size)))
    return partitions


def validate(partitions: list[Partition]) -> None:
    """Raises ValueError when the layout cannot be flashed safely."""
    ordered = sorted(partitions, key=lambda p: p.offset)
    seen_names: set[str] = set()
    for index, partition in enumerate(ordered):
        if partition.name in seen_names:
            raise ValueError(f"duplicate partition name '{partition.name}'")
        seen_names.add(partition.name)
        if len(partition.name) > 16:
            raise ValueError(f"partition name '{partition.name}' is longer than 16 characters")
        alignment = APP_ALIGNMENT if partition.type == "app" else DATA_ALIGNMENT
        if partition.offset % alignment:
            raise ValueError(f"partition '{partition.name}' is not aligned to {hex(alignment)}")
        if partition.end > FLASH_SIZE:
            raise ValueError(f"partition '{partition.name}' extends past the end of flash")
        if index + 1 < len(ordered) and partition.end > ordered[index + 1].offset:
            raise ValueError(f"partition '{partition.name}' overlaps '{ordered[index + 1].name}'")

    ota_slots = [p for p in ordered if p.subtype.startswith("ota_")]
    if not ota_slots:
        raise ValueError("layout has no firmware slots")
    expected = [f"ota_{i}" for i in range(len(ota_slots))]
    if [p.subtype for p in ota_slots] != expected:
        raise ValueError("firmware slots must be numbered ota_0..ota_n without gaps")


LAYOUTS = {
    "multiboot-2slot.csv": (
        2,
        0x640000,
        "# Two 6 MiB firmware slots: the layout for full size builds such as\n"
        "# CrossPoint (5.3 MB in 1.5.0). This is the default layout.",
    ),
    "multiboot-3slot.csv": (
        3,
        0x400000,
        "# Three 4 MiB firmware slots for medium sized firmwares.\n"
        "# CrossPoint release builds do NOT fit here - check the image size first.",
    ),
    "multiboot-5slot.csv": (
        5,
        0x260000,
        "# Five ~2.4 MiB firmware slots for small firmwares (TRMNL, MicroSlate,\n"
        "# slim builds). Verify your image sizes before flashing this layout.",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--slots", type=int, help="number of firmware slots to generate")
    parser.add_argument("--slot-size", type=parse_size, help="size of each firmware slot, e.g. 6M")
    parser.add_argument("--write", action="store_true", help="regenerate the layouts in partitions/")
    parser.add_argument("--check", nargs="*", metavar="CSV", help="validate existing layout files")
    args = parser.parse_args()

    if args.check is not None:
        failures = 0
        for path in args.check:
            try:
                validate(parse_csv(open(path, encoding="utf-8").read()))
                print(f"ok       {path}")
            except ValueError as error:
                failures += 1
                print(f"INVALID  {path}: {error}", file=sys.stderr)
        return 1 if failures else 0

    if args.write:
        target_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "partitions")
        os.makedirs(target_dir, exist_ok=True)
        for filename, (slots, size, comment) in LAYOUTS.items():
            path = os.path.join(target_dir, filename)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(render(build_layout(slots, size), comment))
            print(f"wrote {path}")
        return 0

    if args.slots and args.slot_size:
        print(render(build_layout(args.slots, args.slot_size), "# Generated layout"), end="")
        return 0

    parser.print_help()
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
