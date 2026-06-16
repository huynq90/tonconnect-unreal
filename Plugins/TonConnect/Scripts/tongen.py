#!/usr/bin/env python3
"""
tongen.py — Generate UTonMessageSpec field definitions from a Tact ABI JSON file.

Usage:
    python tongen.py <abi.json> [--out <dir>]

The script reads a Tact ABI JSON file and prints C++ field definitions for each
message type. Paste these into a UTonMessageSpec DataAsset in the UE editor,
or use --out to write .ini files that UE can import directly.

Supported Tact field types → ETonFieldType mapping:
    int<N>   → Int8/Int16/Int32/Int64 (picks smallest that fits, or Int64 + BitWidth)
    uint<N>  → UInt8/UInt16/UInt32/UInt64
    bool     → Bool
    Address  → Address
    Cell     → Bytes (raw)
    String   → Text
    coins    → Coins
    Slice    → Bytes
"""

import json
import sys
import os
from pathlib import Path


TACT_TO_UE = {
    "bool":    "Bool",
    "address": "Address",
    "cell":    "Bytes",
    "slice":   "Bytes",
    "string":  "Text",
    "coins":   "Coins",
}


def tact_type_to_ue(tact_type: str) -> tuple[str, int]:
    """Returns (ETonFieldType name, BitWidth). BitWidth=0 means use type default."""
    tact_lower = tact_type.strip().lower()

    if tact_lower in TACT_TO_UE:
        return TACT_TO_UE[tact_lower], 0

    if tact_lower.startswith("uint"):
        bits = int(tact_lower[4:]) if tact_lower[4:].isdigit() else 32
        if bits <= 8:   return "UInt8",  bits if bits < 8 else 0
        if bits <= 16:  return "UInt16", bits if bits < 16 else 0
        if bits <= 32:  return "UInt32", bits if bits < 32 else 0
        return "UInt64", bits if bits < 64 else 0

    if tact_lower.startswith("int"):
        bits = int(tact_lower[3:]) if tact_lower[3:].isdigit() else 32
        if bits <= 8:   return "Int8",  bits if bits < 8 else 0
        if bits <= 16:  return "Int16", bits if bits < 16 else 0
        if bits <= 32:  return "Int32", bits if bits < 32 else 0
        return "Int64", bits if bits < 64 else 0

    # Unknown — treat as raw bytes
    return "Bytes", 0


def process_message(msg: dict) -> list[dict]:
    fields = []
    for f in msg.get("fields", []):
        ue_type, bit_width = tact_type_to_ue(f.get("type", "uint32"))
        fields.append({
            "name":      f.get("name", ""),
            "ue_type":   ue_type,
            "bit_width": bit_width,
        })
    return fields


def print_cpp(msg_name: str, opcode: int, fields: list[dict]):
    print(f"// --- {msg_name} (opcode 0x{opcode:08X}) ---")
    print(f"// UTonMessageSpec* Spec = ...;")
    print(f"// Spec->Opcode = {opcode};")
    for f in fields:
        bw = f['bit_width']
        print(f"// FTonFieldSpec {{ \"{f['name']}\", ETonFieldType::{f['ue_type']}, {bw} }}")
    print()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    abi_path = Path(sys.argv[1])
    if not abi_path.exists():
        print(f"ERROR: file not found: {abi_path}", file=sys.stderr)
        sys.exit(1)

    with open(abi_path) as f:
        abi = json.load(f)

    messages = abi.get("messages", abi.get("types", []))
    for msg in messages:
        name   = msg.get("name", "Unknown")
        opcode = msg.get("opcode", msg.get("header", 0)) or 0
        if isinstance(opcode, str):
            opcode = int(opcode, 0)
        fields = process_message(msg)
        print_cpp(name, opcode, fields)


if __name__ == "__main__":
    main()
