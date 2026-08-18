#!/usr/bin/env python3
"""Generate SVS V3 wire fixtures without importing ndn-svs production code."""

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INVALID = ROOT / "invalid"


def varnum(value: int) -> bytes:
    if value < 0:
        raise ValueError("negative var-number")
    if value < 0xFD:
        return bytes([value])
    if value <= 0xFFFF:
        return b"\xfd" + value.to_bytes(2, "big")
    if value <= 0xFFFFFFFF:
        return b"\xfe" + value.to_bytes(4, "big")
    return b"\xff" + value.to_bytes(8, "big")


def tlv(typ: int, value: bytes = b"") -> bytes:
    return varnum(typ) + varnum(len(value)) + value


def nni(value: int) -> bytes:
    if value < 0:
        raise ValueError("negative NNI")
    width = 1 if value <= 0xFF else 2 if value <= 0xFFFF else 4 if value <= 0xFFFFFFFF else 8
    return value.to_bytes(width, "big")


def name(uri: str) -> bytes:
    components = []
    for part in (part for part in uri.split("/") if part):
        if part.startswith("v=") and part[2:].isdigit():
            components.append(tlv(54, nni(int(part[2:]))))
        else:
            components.append(tlv(8, part.encode()))
    return tlv(7, b"".join(components))


def seq_entry(boot: int, seq: int) -> bytes:
    return tlv(210, tlv(212, nni(boot)) + tlv(214, nni(seq)))


def vector(entries: list[tuple[str, list[tuple[int, int]]]], *, extra: bytes = b"") -> bytes:
    body = bytearray()
    for node, tuples in sorted(entries, key=lambda item: name(item[0])):
        body.extend(tlv(202, name(node) + b"".join(seq_entry(b, s) for b, s in tuples)))
    body.extend(extra)
    return tlv(201, bytes(body))


def digest_signed_data(data_name: str, content: bytes, *, corrupt=False, omit_signature=False) -> bytes:
    signed = name(data_name) + tlv(20) + tlv(21, content)
    if omit_signature:
        return tlv(6, signed)
    signature_info = tlv(22, tlv(27, nni(0)))
    signature = hashlib.sha256(signed + signature_info).digest()
    if corrupt:
        signature = bytes([signature[0] ^ 0xFF]) + signature[1:]
    return tlv(6, signed + signature_info + tlv(23, signature))


def params(data_name: str, state: bytes, *, corrupt=False, omit_signature=False,
           signed_extensions: bytes = b"", unsigned_trailing: bytes = b"") -> bytes:
    data = digest_signed_data(data_name, state + signed_extensions,
                              corrupt=corrupt, omit_signature=omit_signature)
    return tlv(36, data + unsigned_trailing)


def write(path: Path, wire: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(wire.hex() + "\n", encoding="ascii")


def main() -> None:
    group = "/ndn/svs-v3-test/v=3"
    empty = vector([])
    one = vector([("/node/a", [(1700000000, 1)])])
    multi = vector([
        ("/node/a", [(1700000000, 7), (1700000100, 2)]),
        ("/node/b", [(1700000200, 3)]),
    ])

    write(ROOT / "v3-empty.hex", params(group, empty))
    write(ROOT / "v3-one-node.hex", params(group, one))
    write(ROOT / "v3-multi-epoch.hex", params(group, multi))
    write(ROOT / "v3-unknown-extension.hex",
          params(group, one, signed_extensions=tlv(0xF001, b"opaque")))

    write(INVALID / "wrong-data-name.hex", params("/ndn/svs-v3-test/v=2", one))
    write(INVALID / "missing-signature.hex", params(group, one, omit_signature=True))
    write(INVALID / "invalid-signature.hex", params(group, one, corrupt=True))
    write(INVALID / "raw-state-vector.hex", tlv(36, one))
    write(INVALID / "malformed-content.hex", params(group, tlv(205, b"not-state-vector")))
    write(INVALID / "seq-zero.hex", params(group, vector([("/node/a", [(1700000000, 0)])])))
    write(INVALID / "future-bootstrap.hex", params(group, vector([("/node/a", [(4102444800, 1)])])))
    write(INVALID / "duplicate-state-vector.hex", tlv(36, digest_signed_data(group, one + one)))
    write(INVALID / "unknown-core-tlv.hex", params(group, vector([], extra=tlv(0xF000, b"unknown"))))
    write(INVALID / "truncated-extension.hex",
          params(group, one, signed_extensions=varnum(0xF001) + varnum(8) + b"abc"))
    write(INVALID / "unsigned-trailing-extension.hex",
          params(group, one, unsigned_trailing=tlv(0xF001, b"opaque")))


if __name__ == "__main__":
    main()
