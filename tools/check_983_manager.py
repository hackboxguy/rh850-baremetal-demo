#!/usr/bin/env python3
"""
Host-side verification for 983_manager generated assets.

Checks:
  - panels.json loads cleanly and selector uniqueness holds
  - versioned EDID binaries exist and have valid per-block checksums
  - optimized block-based source expansion matches the BIOS-derived flat stream
  - generated profile_data.c/.h are up to date with the current generator inputs
  - committed golden_reference.json still matches the built-in profile baseline
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys

import gen_983_manager_profiles as gen


GOLDEN_PATH = (
    gen.REPO_ROOT / "rh850-baremetal-demo" / "app" / "983_manager" / "golden_reference.json"
)
RELEASE_MAP_PATH = (
    gen.REPO_ROOT
    / "rh850-baremetal-demo"
    / "output"
    / "983HH"
    / "983_manager"
    / "release"
    / "983HH_983_manager.map"
)


def fail(message: str):
    print(f"FAIL: {message}")
    return False


def ok(message: str):
    print(f"OK:   {message}")
    return True


def sha256_hex_bytes(data: bytes):
    return hashlib.sha256(data).hexdigest()


def sha256_hex_ops(ops):
    parts = []

    for op in ops:
        parts.append(f"{op[0]}:{op[1]:02X}:{op[2]:02X}:{op[3]:02X}")

    return sha256_hex_bytes("\n".join(parts).encode("utf-8"))


def parse_release_total_bytes(path):
    if not path.exists():
        return None

    total = 0
    found = False

    for line in path.read_text().splitlines():
        if line.startswith("SECTION "):
            found = True
            continue

        if not found:
            continue

        if not line.strip():
            break

        if not line.startswith(" "):
            continue

        fields = line.split()
        if len(fields) >= 3:
            total += int(fields[2], 16)

    return total if total != 0 else None


def build_golden_reference(profiles, resolved_sources, profile_edids):
    reference = {
        "schema_version": 1,
        "profile_count": len(profiles),
        "profiles": [],
    }

    release_total = parse_release_total_bytes(RELEASE_MAP_PATH)
    if release_total is not None:
        release_max = ((release_total + 1023) // 1024) * 1024
        reference["release"] = {
            "map_path": "output/983HH/983_manager/release/983HH_983_manager.map",
            "current_total_bytes": release_total,
            "max_total_bytes": release_max,
        }

    for profile in profiles:
        ops = resolved_sources[profile.bios_source].ops
        edid_bytes = bytes(profile_edids[profile.key])
        reference["profiles"].append(
            {
                "key": profile.key,
                "display_name": profile.display_name,
                "bios_source": profile.bios_source,
                "selector": {
                    "core_mask": profile.core_mask,
                    "dip7_on": bool(profile.dip7_on),
                },
                "edid_len": len(edid_bytes),
                "edid_sha256": sha256_hex_bytes(edid_bytes),
                "flat_op_count": len(ops),
                "flat_ops_sha256": sha256_hex_ops(ops),
            }
        )

    return reference


def verify_edid_assets(profiles):
    success = True
    profile_edids = {}
    corrections_by_profile = {}

    for profile in profiles:
        path = profile.edid_bin_path
        if not path.exists():
            success &= fail(f"{profile.key}: missing EDID asset {path}")
            continue

        edid_bytes = list(path.read_bytes())
        profile_edids[profile.key] = edid_bytes

        if len(edid_bytes) < 128 or (len(edid_bytes) % 128) != 0:
            success &= fail(f"{profile.key}: invalid EDID length {len(edid_bytes)}")
            continue

        normalized = list(edid_bytes)
        corrections = gen.normalize_edid(profile.key, normalized)
        corrections_by_profile[profile.key] = corrections

        if corrections:
            summary = ", ".join(
                f"block {block} -> 0x{checksum:02X}" for block, checksum in corrections
            )
            success &= fail(f"{profile.key}: EDID checksum mismatch ({summary})")
            continue

        success &= ok(f"{profile.key}: EDID {len(edid_bytes)} bytes with valid checksums")

    return success, profile_edids, corrections_by_profile


def verify_source_blocks(resolved_sources):
    success = True
    source_blocks = gen.build_source_blocks(resolved_sources)
    runtime_blocks = gen.build_runtime_blocks(source_blocks)
    flattened_runtime = gen.flatten_runtime_blocks(runtime_blocks)

    for source_key in sorted(resolved_sources):
        block_refs = source_blocks[source_key]
        flattened = []

        for _symbol, ops in block_refs:
            flattened.extend(ops)

        if flattened != resolved_sources[source_key].ops:
            success &= fail(f"{source_key}: optimized block expansion differs from flat BIOS stream")
            continue

        success &= ok(f"{source_key}: block expansion matches flat BIOS stream")

        if flattened_runtime[source_key] != resolved_sources[source_key].ops:
            success &= fail(f"{source_key}: packed runtime expansion differs from flat BIOS stream")
            continue

        success &= ok(f"{source_key}: packed runtime expansion matches flat BIOS stream")

    return success


def verify_generated_files(profiles, resolved_sources, corrections_by_profile, profile_edids):
    success = True

    expected_h = gen.render_header(profiles)
    expected_c = gen.render_c(profiles, resolved_sources, corrections_by_profile, profile_edids)

    actual_h = gen.OUT_H_PATH.read_text()
    actual_c = gen.OUT_C_PATH.read_text()

    if actual_h != expected_h:
        success &= fail(f"{gen.OUT_H_PATH.name} is out of date; re-run gen_983_manager_profiles.py")
    else:
        success &= ok(f"{gen.OUT_H_PATH.name} is up to date")

    if actual_c != expected_c:
        success &= fail(f"{gen.OUT_C_PATH.name} is out of date; re-run gen_983_manager_profiles.py")
    else:
        success &= ok(f"{gen.OUT_C_PATH.name} is up to date")

    return success


def verify_golden_reference(reference):
    success = True

    if not GOLDEN_PATH.exists():
        return fail(f"missing golden reference {GOLDEN_PATH}")

    golden = json.loads(GOLDEN_PATH.read_text())

    if golden.get("schema_version") != reference.get("schema_version"):
        success &= fail("golden schema_version mismatch")
    else:
        success &= ok(f"golden schema_version {golden['schema_version']}")

    if golden.get("profile_count") != reference.get("profile_count"):
        success &= fail(
            f"golden profile_count {golden.get('profile_count')} != {reference.get('profile_count')}"
        )
    else:
        success &= ok(f"golden profile_count {golden['profile_count']}")

    golden_profiles = {entry["key"]: entry for entry in golden.get("profiles", [])}
    reference_profiles = {entry["key"]: entry for entry in reference["profiles"]}

    if set(golden_profiles) != set(reference_profiles):
        success &= fail("golden profile key set differs from current manifest")
    else:
        success &= ok("golden profile key set matches current manifest")

    for key in sorted(reference_profiles):
        current = reference_profiles[key]
        golden_entry = golden_profiles.get(key)

        if golden_entry != current:
            success &= fail(f"{key}: golden reference drift detected")
        else:
            success &= ok(f"{key}: golden selector / EDID / op hashes match")

    golden_release = golden.get("release")
    if golden_release:
        current_total = parse_release_total_bytes(RELEASE_MAP_PATH)
        if current_total is None:
            success &= ok("release size check skipped (no release map present)")
        elif current_total > int(golden_release["max_total_bytes"]):
            success &= fail(
                f"release size {current_total} exceeds golden max {golden_release['max_total_bytes']}"
            )
        else:
            success &= ok(
                f"release size {current_total} within golden max {golden_release['max_total_bytes']}"
            )

    return success


def refresh_golden_reference(reference):
    text = json.dumps(reference, indent=2, sort_keys=False) + "\n"
    gen.write_text_if_changed(GOLDEN_PATH, text)
    print(f"WROTE: {GOLDEN_PATH}")


def main():
    parser = argparse.ArgumentParser(description="Verify 983_manager generated assets")
    parser.add_argument(
        "--refresh-golden",
        action="store_true",
        help="Update app/983_manager/golden_reference.json from the current verified baseline",
    )
    args = parser.parse_args()

    success = True

    profiles = gen.load_manifest()
    success &= ok(f"loaded {len(profiles)} profiles from panels.json")

    instructions, labels = gen.parse_bios()
    resolved_sources = {}

    for bios_source in sorted({profile.bios_source for profile in profiles}):
        ops = gen.resolve_source_ops(bios_source, instructions, labels)
        ops, default_edid = gen.extract_edid_blob(bios_source, ops)
        gen.normalize_edid(bios_source, default_edid)
        resolved_sources[bios_source] = gen.ResolvedSource(
            key=bios_source,
            ops_symbol=f"g_ops_src_{bios_source}",
            ops=ops,
            default_edid=default_edid,
        )

    edid_ok, profile_edids, corrections_by_profile = verify_edid_assets(profiles)
    success &= edid_ok
    success &= verify_source_blocks(resolved_sources)
    success &= verify_generated_files(profiles, resolved_sources, corrections_by_profile, profile_edids)

    reference = build_golden_reference(profiles, resolved_sources, profile_edids)

    if args.refresh_golden:
        if not success:
            return 1
        refresh_golden_reference(reference)
        success &= verify_golden_reference(reference)
    else:
        success &= verify_golden_reference(reference)

    if not success:
        return 1

    print("PASS: 983_manager verification complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
