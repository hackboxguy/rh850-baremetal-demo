#!/usr/bin/env python3
"""
Host-side verification for 983_manager generated assets.

Checks:
  - panels.json loads cleanly and selector uniqueness holds
  - versioned EDID binaries exist and have valid per-block checksums
  - optimized block-based source expansion matches the BIOS-derived flat stream
  - generated profile_data.c/.h are up to date with the current generator inputs
"""

from __future__ import annotations

import sys

import gen_983_manager_profiles as gen


def fail(message: str):
    print(f"FAIL: {message}")
    return False


def ok(message: str):
    print(f"OK:   {message}")
    return True


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

    for source_key in sorted(resolved_sources):
        block_refs = source_blocks[source_key]
        flattened = []

        for _symbol, ops in block_refs:
            flattened.extend(ops)

        if flattened != resolved_sources[source_key].ops:
            success &= fail(f"{source_key}: optimized block expansion differs from flat BIOS stream")
            continue

        success &= ok(f"{source_key}: block expansion matches flat BIOS stream")

    return success, source_blocks


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


def main():
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

    success, profile_edids, corrections_by_profile = verify_edid_assets(profiles)
    blocks_ok, _source_blocks = verify_source_blocks(resolved_sources)
    success &= blocks_ok
    success &= verify_generated_files(profiles, resolved_sources, corrections_by_profile, profile_edids)

    if not success:
        return 1

    print("PASS: 983_manager verification complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
