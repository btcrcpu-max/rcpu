#!/usr/bin/env python3

"""Fail-closed read-back of a published release (REL-02).

The publish job in .github/workflows/release-full.yml uploads the installers,
the merged checksums and the detached signature, and then must prove that
GitHub actually serves them. Uploading is not proof: an asset can be dropped,
truncated or left behind from an earlier attempt, and a silently incomplete
release is worse than a failed workflow.

Why the module exists instead of an inline heredoc: the first version of this
check ran `gh` in a job without actions/checkout. `gh` infers the repository
from a git remote and there was none, so v1.4.18 failed at the very last step
with "fatal: not a git repository" after every asset had already been
published. Keeping the logic in a module with unit tests (test_readback.py)
makes it runnable in ordinary pull-request CI, and every GitHub call here pins
--repo explicitly.

Usage (as the release job runs it):

    python3 contrib/verify-binaries/release_readback.py \
        --repo "$GITHUB_REPOSITORY" --tag "$GITHUB_REF_NAME" \
        --expected-dir linux --expected-dir windows \
        --workdir readback --signature-key RCPU-DEV-GPG-KEY.asc
"""

import argparse
import json
import pathlib
import subprocess
import sys

CHECKSUM_FILES = ("SHA256SUMS.txt", "SHA256SUMS.txt.asc")


class ReadbackError(Exception):
    """A published release does not match what the job built."""


def expected_asset_names(directories):
    """Asset names the release must expose: every file in the given directories
    plus the merged checksum file and its detached signature."""
    names = []
    for directory in directories:
        path = pathlib.Path(directory)
        if not path.is_dir():
            raise ReadbackError(f"expected artifact directory {directory!r} does not exist")
        names.extend(p.name for p in path.iterdir() if p.is_file())
    return sorted(names + list(CHECKSUM_FILES))


def audit(payload, expected=None):
    """Validate a `gh release view --json isDraft,isPrerelease,assets` payload."""
    if payload.get("isDraft"):
        raise ReadbackError("release is still a draft")
    assets = payload.get("assets") or []
    names = sorted(asset["name"] for asset in assets)
    if expected is not None:
        missing = sorted(set(expected) - set(names))
        extra = sorted(set(names) - set(expected))
        if missing or extra:
            raise ReadbackError(f"asset mismatch; missing={missing} extra={extra}")
    empty = [asset["name"] for asset in assets if asset.get("size", 0) <= 0]
    if empty:
        raise ReadbackError(f"zero-byte assets {empty}")
    return names


def gh(*args):
    """Run gh with an explicit --repo already present in args.

    gh infers OWNER/REPO from the git remote; a job without a checkout has no
    remote and aborts with "fatal: not a git repository". Refusing the call here
    turns that release-time failure into an immediate, unit-testable error.
    """
    if "--repo" not in args:
        raise ReadbackError("every gh call must pin --repo")
    result = subprocess.run(["gh", *args], check=True, capture_output=True, text=True)
    return result.stdout


def main(argv=None):
    parser = argparse.ArgumentParser(description="verify a published release")
    parser.add_argument("--repo", required=True, help="OWNER/REPO; never inferred from a remote")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--expected-dir", action="append", default=[],
                        help="directory of artifacts that must appear as assets (repeatable)")
    parser.add_argument("--workdir", default="readback")
    parser.add_argument("--signature-key",
                        help="armored key used to verify SHA256SUMS.txt.asc")
    args = parser.parse_args(argv)

    try:
        workdir = pathlib.Path(args.workdir)
        workdir.mkdir(parents=True, exist_ok=True)

        payload = json.loads(gh("release", "view", args.tag, "--repo", args.repo,
                                "--json", "isDraft,isPrerelease,assets"))
        expected = expected_asset_names(args.expected_dir) if args.expected_dir else None
        names = audit(payload, expected)
        print(f"readback: {len(names)} assets present, all non-empty")

        gh("release", "download", args.tag, "--repo", args.repo,
           "--pattern", "SHA256SUMS.txt", "--pattern", "SHA256SUMS.txt.asc",
           "--dir", str(workdir), "--clobber")

        local = pathlib.Path("SHA256SUMS.txt")
        published = workdir / "SHA256SUMS.txt"
        if local.is_file() and local.read_bytes() != published.read_bytes():
            raise ReadbackError("published SHA256SUMS.txt differs from the merged local file")

        if args.signature_key:
            subprocess.run(["gpg", "--batch", "--import", args.signature_key],
                           check=True, capture_output=True)
            subprocess.run(["gpg", "--batch", "--verify",
                            str(workdir / "SHA256SUMS.txt.asc"), str(published)],
                           check=True, capture_output=True)
            print("readback: published checksum file matches and its signature verifies")
        else:
            print("readback: published checksum file matches")
    except (ReadbackError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        sys.exit(f"readback failed: {exc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
