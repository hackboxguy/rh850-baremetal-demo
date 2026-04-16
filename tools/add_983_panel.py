#!/usr/bin/env python3
"""
Import a new EDID binary into the 983_manager manifest flow.

Typical usage:
  python3 rh850-baremetal-demo/tools/add_983_panel.py \
      --key my_new_panel \
      --display-name "DIP4 13.3in 1920x720" \
      --base-profile dip4_10g8 \
      --edid /path/to/panel.bin \
      --core-mask 16 \
      --dip7-on 0 \
      --generate

For a simple replacement of an existing profile, reuse the existing key and
pass --replace.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "panels.json"
GENERATOR_PATH = REPO_ROOT / "rh850-baremetal-demo" / "tools" / "gen_983_manager_profiles.py"


def normalize_edid(edid_bytes: bytearray):
    corrections = []

    if not edid_bytes:
        raise ValueError("EDID is empty")

    if (len(edid_bytes) % 128) != 0:
        raise ValueError(f"EDID length {len(edid_bytes)} is not block aligned")

    block_count = min(1 + edid_bytes[126], len(edid_bytes) // 128)
    for block in range(block_count):
        start = block * 128
        end = start + 128
        checksum = sum(edid_bytes[start:end]) & 0xFF
        if checksum != 0:
            new_checksum = (-sum(edid_bytes[start:end - 1])) & 0xFF
            edid_bytes[end - 1] = new_checksum
            corrections.append((block, new_checksum))

    return corrections


def load_manifest():
    data = json.loads(MANIFEST_PATH.read_text())
    profiles = data.get("profiles")
    if not isinstance(profiles, list):
        raise ValueError("panels.json is missing 'profiles'")
    return data, profiles


def find_profile(profiles, key):
    for idx, profile in enumerate(profiles):
        if profile["key"] == key:
            return idx, profile
    return -1, None


def main():
    parser = argparse.ArgumentParser(description="Add or replace a 983_manager panel profile")
    parser.add_argument("--key", help="profile key (lowercase snake_case)")
    parser.add_argument("--display-name", help="user-facing profile name")
    parser.add_argument("--base-profile", help="existing manifest profile to reuse for non-EDID init")
    parser.add_argument("--edid", type=Path, help="path to 128/256-byte EDID .bin file")
    parser.add_argument("--core-mask", type=int, help="new selector core_mask (decimal)")
    parser.add_argument("--dip7-on", type=int, choices=[0, 1], help="selector dip7_on value")
    parser.add_argument("--replace", action="store_true", help="replace an existing profile with the same key")
    parser.add_argument("--generate", action="store_true", help="run the profile generator after updating the manifest")
    parser.add_argument("--list-base-profiles", action="store_true", help="list existing manifest profile keys")
    args = parser.parse_args()

    data, profiles = load_manifest()

    if args.list_base_profiles:
        for profile in profiles:
            print(profile["key"])
        return

    required = [args.key, args.display_name, args.base_profile, args.edid]
    if any(value is None for value in required):
        parser.error("--key, --display-name, --base-profile, and --edid are required")

    if not args.edid.exists():
        raise FileNotFoundError(args.edid)

    base_idx, base = find_profile(profiles, args.base_profile)
    if base is None:
        raise ValueError(f"Unknown base profile '{args.base_profile}'")

    existing_idx, existing = find_profile(profiles, args.key)
    if existing is not None and not args.replace:
        raise ValueError(f"Profile '{args.key}' already exists; pass --replace to update it")
    if existing is None and args.replace:
        raise ValueError(f"Profile '{args.key}' does not exist; cannot replace")

    edid_bytes = bytearray(args.edid.read_bytes())
    corrections = normalize_edid(edid_bytes)

    core_mask = args.core_mask if args.core_mask is not None else int(base["selection"]["core_mask"])
    dip7_on = bool(args.dip7_on) if args.dip7_on is not None else bool(base["selection"]["dip7_on"])
    edid_rel = f"rh850-baremetal-demo/app/983_manager/edid/{args.key}.bin"
    edid_out_path = REPO_ROOT / edid_rel
    edid_out_path.parent.mkdir(parents=True, exist_ok=True)
    edid_out_path.write_bytes(edid_bytes)

    entry = {
        "key": args.key,
        "display_name": args.display_name,
        "bios_source": base["bios_source"],
        "selection": {
            "core_mask": core_mask,
            "dip7_on": dip7_on,
        },
        "edid_bin": edid_rel,
    }

    if existing is not None:
        profiles[existing_idx] = entry
    else:
        profiles.insert(base_idx + 1, entry)

    seen = set()
    for profile in profiles:
        selector = (int(profile["selection"]["core_mask"]), bool(profile["selection"]["dip7_on"]))
        if selector in seen:
            raise ValueError(
                f"Duplicate selector after update: core_mask=0x{selector[0]:02X} dip7_on={int(selector[1])}"
            )
        seen.add(selector)

    MANIFEST_PATH.write_text(json.dumps(data, indent=2) + "\n")

    print(f"Updated manifest: {MANIFEST_PATH}")
    print(f"Wrote EDID bin   : {edid_out_path}")
    if corrections:
        for block, checksum in corrections:
            print(f"Corrected block {block} checksum to 0x{checksum:02X}")

    if args.generate:
        subprocess.run([sys.executable, str(GENERATOR_PATH)], check=True)
        print("Regenerated profile_data.c/.h")


if __name__ == "__main__":
    main()
