"""Write the private CI boot ROM without printing its contents."""

from __future__ import annotations

import argparse
import base64
import binascii
import os
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    encoded = os.environ.get("N64_PIF_NTSC", "").strip()
    if not encoded:
        parser.error("The N64_PIF_NTSC repository secret is required for ROM validation.")
    try:
        firmware = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error):
        parser.error("N64_PIF_NTSC must contain a base64-encoded boot ROM.")
    if len(firmware) not in (0x7C0, 0x800):
        parser.error("The boot ROM must contain 1984 or 2048 bytes.")
    arguments.destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(arguments.destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(firmware[:0x7C0])
    except OSError as error:
        parser.error(f"Cannot prepare the boot ROM: {error.strerror}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
