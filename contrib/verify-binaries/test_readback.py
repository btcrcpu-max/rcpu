#!/usr/bin/env python3

"""Offline tests for release_readback.py.

The regression these lock down: the read-back ran `gh` in a job without a
checkout, so gh could not infer the repository and the release failed at the
last step. The tests stay offline and cover the asset audit, which is the part
that decides whether a release is accepted.

Run from the repository root:

    python3 contrib/verify-binaries/test_readback.py
"""

import importlib.util
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("rcpu_readback", HERE / "release_readback.py")
readback = importlib.util.module_from_spec(spec)
spec.loader.exec_module(readback)

FAILURES = []


def check(condition: bool, msg: str) -> None:
    if condition:
        print(f"PASS {msg}")
    else:
        print(f"FAIL {msg}")
        FAILURES.append(msg)


def raises(fn, expected: str) -> bool:
    try:
        fn()
    except readback.ReadbackError as exc:
        return expected in str(exc)
    return False


def payload(**assets):
    return {
        "isDraft": False,
        "isPrerelease": False,
        "assets": [{"name": name, "size": size} for name, size in assets.items()],
    }


def test_expected_asset_names() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        linux = Path(tmp) / "linux"
        windows = Path(tmp) / "windows"
        linux.mkdir()
        windows.mkdir()
        (linux / "rcpu-1.4.18-x86_64-linux-gnu.tgz").write_bytes(b"x")
        (windows / "rcpu-1.4.18-win64-setup-unsigned.exe").write_bytes(b"x")
        (windows / "nested").mkdir()

        names = readback.expected_asset_names([str(linux), str(windows)])
        check(names == sorted([
            "rcpu-1.4.18-x86_64-linux-gnu.tgz",
            "rcpu-1.4.18-win64-setup-unsigned.exe",
            "SHA256SUMS.txt",
            "SHA256SUMS.txt.asc",
        ]), "expected asset names combine both directories with the checksum files")

    check(raises(lambda: readback.expected_asset_names(["does-not-exist"]),
                 "does not exist"),
          "a missing artifact directory is a hard error")


def test_audit_accepts_complete_release() -> None:
    names = readback.audit(payload(**{"a.tgz": 10, "SHA256SUMS.txt": 5}),
                           expected=["a.tgz", "SHA256SUMS.txt"])
    check(names == ["SHA256SUMS.txt", "a.tgz"], "a complete release passes the audit")


def test_audit_rejects_draft() -> None:
    data = payload(**{"a.tgz": 10})
    data["isDraft"] = True
    check(raises(lambda: readback.audit(data), "still a draft"),
          "a draft release fails closed")


def test_audit_rejects_missing_and_extra_assets() -> None:
    check(raises(lambda: readback.audit(payload(**{"a.tgz": 10}), expected=["a.tgz", "b.tgz"]),
                 "missing=['b.tgz']"),
          "a missing asset fails closed")
    check(raises(lambda: readback.audit(payload(**{"a.tgz": 10, "old.exe": 3}), expected=["a.tgz"]),
                 "extra=['old.exe']"),
          "an asset left over from an earlier attempt fails closed")


def test_audit_rejects_zero_byte_assets() -> None:
    check(raises(lambda: readback.audit(payload(**{"a.tgz": 0})), "zero-byte assets ['a.tgz']"),
          "a zero-byte asset fails closed")


def test_gh_requires_repo() -> None:
    check(raises(lambda: readback.gh("release", "view", "v1.4.18"), "must pin --repo"),
          "a gh call without --repo is refused before it runs")


def main() -> int:
    test_expected_asset_names()
    test_audit_accepts_complete_release()
    test_audit_rejects_draft()
    test_audit_rejects_missing_and_extra_assets()
    test_audit_rejects_zero_byte_assets()
    test_gh_requires_repo()
    if FAILURES:
        print(f"\n{len(FAILURES)} failure(s)")
        return 1
    print("\nall tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
