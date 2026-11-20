#!/usr/bin/env python3

"""Offline tests for verify.py.

The upstream Bitcoin version of this file downloaded bitcoin-core-22.0 and
asserted its hashes; that cannot work for RCPU. These tests stay offline and
cover the RCPU-specific decisions: the GitHub release URL layout, the single
pinned signer, checksum parsing and the fail-closed paths.

Run from contrib/verify-binaries/ or from the repository root:

    python3 contrib/verify-binaries/test.py
"""

import argparse
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("rcpu_verify", HERE / "verify.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

FAILURES = []


def check(condition: bool, msg: str) -> None:
    if condition:
        print(f"PASS {msg}")
    else:
        print(f"FAIL {msg}")
        FAILURES.append(msg)


def test_version_parsing() -> None:
    check(verify.parse_version_string("1.4.17") == ("1.4.17", "", ""),
          "version 1.4.17 parses without platform")
    check(verify.parse_version_string("1.4.17-win64") == ("1.4.17", "", "win64"),
          "version 1.4.17-win64 parses platform filter")
    check(verify.parse_version_string("1.4.17-rc1-linux") == ("1.4.17", "rc1", "linux"),
          "version with rc and platform parses")


def test_pinned_signer() -> None:
    fpr = verify.RELEASE_KEY_FINGERPRINT
    check(verify.normalise_key("934d 5bc9 5dd4 b3ac fef5 21b9 5476 3350 1fe4 b8ee") == fpr,
          "spaced lowercase fingerprint normalises")
    check(verify.key_matches_pin("547633501FE4B8EE"), "16-hex long key id matches the pin")
    check(verify.key_matches_pin(fpr.lower()), "full fingerprint matches the pin")
    check(not verify.key_matches_pin("DEADBEEFDEADBEEF"), "an unrelated key does not match")
    check(not verify.key_matches_pin(None), "a missing key does not match")


def test_release_asset_names() -> None:
    check(verify.SUMS_FILENAME == "SHA256SUMS.txt", "checksum asset is SHA256SUMS.txt")
    check(verify.SIGNATUREFILENAME == "SHA256SUMS.txt.asc",
          "signature asset is SHA256SUMS.txt.asc")
    check(set(verify.PLATFORM_TOKENS) >= {"linux", "win"},
          "linux and win are required platforms")


def test_sums_file_parsing() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        sums = Path(tmp) / "SHA256SUMS.txt"
        sums.write_text(
            "aa11  rcpu-1.4.17-x86_64-linux-gnu.tgz\n"
            "bb22  rcpu-1.4.17-win64-setup.exe\n",
            encoding="utf8")
        parsed = verify.parse_sums_file(str(sums), [])
        check(parsed == [["aa11", "rcpu-1.4.17-x86_64-linux-gnu.tgz"],
                         ["bb22", "rcpu-1.4.17-win64-setup.exe"]],
              "checksum file parses into (hash, filename) pairs")

        linux = verify.parse_sums_file(str(sums), ["linux"])
        check([name for _, name in linux] == ["rcpu-1.4.17-x86_64-linux-gnu.tgz"],
              "platform filter selects only matching assets")

        # verify_binary_hashes must fail closed on a mismatch.
        binary = Path(tmp) / "rcpu-1.4.17-x86_64-linux-gnu.tgz"
        binary.write_bytes(b"not the expected payload")
        status, _ = verify.verify_binary_hashes([["aa11", str(binary)]])
        check(status == verify.ReturnCode.INTEGRITY_FAILURE,
              "hash mismatch returns INTEGRITY_FAILURE")


def test_published_url_layout() -> None:
    """The pub handler must build a GitHub release URL, not a /bin/ tree."""
    seen = []

    def fake_download(url, local_file):
        seen.append(url)
        return (False, "simulated failure")

    original = verify.download_with_wget
    verify.download_with_wget = fake_download
    cwd = os.getcwd()
    try:
        args = argparse.Namespace(
            version="1.4.17",
            require_all_hosts=False,
            host=None,
        )
        status = verify.verify_published_handler(args)
    finally:
        verify.download_with_wget = original
        os.chdir(cwd)

    check(status == verify.ReturnCode.FILE_GET_FAILED,
          "a failed download returns FILE_GET_FAILED")
    check(bool(seen), "the handler attempted a download")
    expected = f"/v1.4.17/{verify.SIGNATUREFILENAME}"
    check(seen and seen[0].endswith(expected),
          f"download URL ends with {expected!r} (got {seen[0] if seen else 'nothing'})")


def main() -> int:
    test_version_parsing()
    test_pinned_signer()
    test_release_asset_names()
    test_sums_file_parsing()
    test_published_url_layout()
    if FAILURES:
        print(f"\n{len(FAILURES)} failure(s)")
        return 1
    print("\nall tests passed")
    return 0


if __name__ == '__main__':
    sys.exit(main())
