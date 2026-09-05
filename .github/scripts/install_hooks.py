#!/usr/bin/env python3
"""
Install pre-commit hooks to prevent Chinese characters in commits.
Run: python .github/scripts/install_hooks.py
"""

import os
import sys
import stat

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HOOKS_DIR = os.path.join(REPO_ROOT, '.git', 'hooks')
HOOK_SOURCE = os.path.join(os.path.dirname(__file__), 'pre-commit-hook')
HOOK_DEST = os.path.join(HOOKS_DIR, 'pre-commit')


def main():
    print("=" * 50)
    print("  RCPU Git Hook Installer")
    print("  Chinese character pre-commit guard")
    print("=" * 50)

    # Ensure .git directory exists
    git_dir = os.path.join(REPO_ROOT, '.git')
    if not os.path.isdir(git_dir):
        print("\n  Error: Not a git repository (no .git directory)")
        print("  Run this from within a cloned repository")
        sys.exit(1)

    # Create hooks directory if needed
    os.makedirs(HOOKS_DIR, exist_ok=True)

    # Check source file exists
    if not os.path.isfile(HOOK_SOURCE):
        print(f"\n  Error: Hook source not found at:\n    {HOOK_SOURCE}")
        sys.exit(1)

    # Check if hook already exists
    if os.path.exists(HOOK_DEST):
        print(f"\n  ⚠ Existing hook found at:\n    {HOOK_DEST}")
        print(f"\n  Overwrite existing hook? [y/N]: ", end='', flush=True)
        # Auto-overwrite in CI/non-interactive mode
        try:
            response = input().strip().lower()
        except EOFError:
            response = 'y'

        if response != 'y':
            print("\n  Installation cancelled.")
            sys.exit(0)

    # Copy hook file
    with open(HOOK_SOURCE, 'r') as src:
        content = src.read()

    with open(HOOK_DEST, 'w') as dest:
        dest.write(content)

    # Make executable (Unix/macOS)
    if sys.platform != 'win32':
        os.chmod(HOOK_DEST, os.stat(HOOK_DEST).st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)

    print(f"\n  ✅ Pre-commit hook installed!")
    print(f"     Location: {HOOK_DEST}")

    # Verify it works
    print(f"\n  Testing hook...")
    test_file = os.path.join(REPO_ROOT, '.github', 'scripts', 'check_chinese.py')
    if os.path.isfile(test_file):
        result = os.system(f'cd "{REPO_ROOT}" && "{sys.executable}" ".github/scripts/pre-commit-hook" 2>&1')
        if result == 0:
            print(f"  ✅ Hook runs successfully")
        else:
            print(f"  ⚠ Hook exited with non-zero (may be expected in some cases)")

    print(f"\n  How it works:")
    print(f"    • Before every commit, hook scans staged files for Chinese characters")
    print(f"    • If Chinese characters are found, commit is blocked with error message")
    print(f"    • Use 'git commit --no-verify' to bypass (NOT recommended)")
    print(f"\n  To uninstall:")
    print(f"    rm {HOOK_DEST}")
    print()


if __name__ == '__main__':
    main()
