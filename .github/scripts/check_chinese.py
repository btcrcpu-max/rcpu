#!/usr/bin/env python3
"""
Chinese Character Detector - Scans repository files for Chinese characters.
Exits with code 1 if any Chinese characters are found, 0 otherwise.

Exclusions:
  - src/qt/locale/ (Bitcoin's official translation files)
  - Binary files (images, compiled binaries, etc.)
  - Lock files, vendor directories
"""

import os
import sys
import re
import fnmatch

# Patterns to exclude from scanning
EXCLUDED_PATHS = [
    'src/qt/locale/',
    'src/qt/android/',
    'src/qt/res/',
    'share/pixmaps/',
    'src/minisketch/doc/',
    '.git/',
    'node_modules/',
    'vendor/',
    '__pycache__/',
    '.github/scripts/check_chinese.py',
    '.github/scripts/check_chinese_git.py',
]

# File extensions that should be scanned
SCAN_EXTENSIONS = {
    '.py', '.js', '.ts', '.c', '.h', '.cpp', '.cc', '.java',
    '.html', '.css', '.scss', '.less',
    '.sh', '.bash', '.zsh',
    '.md', '.txt', '.rst', '.adoc',
    '.yml', '.yaml', '.json', '.xml', '.toml', '.ini', '.cfg', '.conf',
    '.jsx', '.tsx', '.vue', '.svelte',
    '.go', '.rs', '.rb', '.php', '.pl', '.lua',
    '.sql', '.graphql', '.proto',
}

# Binary file extensions to skip
BINARY_EXTENSIONS = {
    '.png', '.jpg', '.jpeg', '.gif', '.ico', '.svg', '.webp', '.bmp',
    '.exe', '.dll', '.so', '.dylib', '.o', '.a', '.lib', '.obj',
    '.gz', '.tar', '.zip', '.bz2', '.xz', '.7z', '.rar',
    '.pdf', '.doc', '.docx', '.xls', '.xlsx',
    '.ttf', '.otf', '.woff', '.woff2',
    '.mp3', '.mp4', '.wav', '.avi', '.mov',
    '.pyc', '.pyo', '.class',
    '.git', '.lock', '.sum',
}

# Specific files to always skip
SKIP_FILES = {
    'package-lock.json',
    'yarn.lock',
    'Gemfile.lock',
    'Cargo.lock',
    'go.sum',
}


def should_scan(filepath: str) -> bool:
    """Determine if a file should be scanned for Chinese characters."""
    # Normalize path separators
    filepath = filepath.replace('\\', '/')

    # Check excluded paths
    for excluded in EXCLUDED_PATHS:
        if excluded in filepath:
            return False

    # Get filename and extension
    filename = os.path.basename(filepath)
    _, ext = os.path.splitext(filename.lower())

    # Skip lock files
    if filename in SKIP_FILES:
        return False

    # Skip binary extensions
    if ext in BINARY_EXTENSIONS:
        return False

    # Skip large binary files by name
    if ext == '' and any(b in filename for b in ['minerd', 'rcpud', 'rcpu-cli', 'rcpu-tx', 'rcpu-wallet', 'xmrig']):
        return False

    # Only scan recognized text extensions
    if ext and ext not in SCAN_EXTENSIONS:
        return False

    return True


def find_chinese_in_file(filepath: str) -> list:
    """Find Chinese characters in a file. Returns list of (line_number, line_content)."""
    chinese_pattern = re.compile(r'[\u4e00-\u9fff]')
    results = []

    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                matches = chinese_pattern.findall(line)
                if matches:
                    # Show only first 200 chars of the line
                    snippet = line.strip()[:200]
                    results.append((line_num, snippet, matches))
    except (UnicodeDecodeError, IOError):
        pass  # Skip binary files

    return results


def scan_repo(repo_root: str, files_to_check: list = None) -> dict:
    """
    Scan repository for Chinese characters.

    Args:
        repo_root: Root directory of the repository
        files_to_check: Specific files to check (if None, scans all)

    Returns:
        dict with {filepath: [(line_number, snippet, chars), ...]}
    """
    issues = {}
    total_files_scanned = 0

    if files_to_check:
        for filepath in files_to_check:
            full_path = os.path.join(repo_root, filepath)
            if not os.path.isfile(full_path):
                continue
            if not should_scan(filepath):
                continue

            chinese_lines = find_chinese_in_file(full_path)
            if chinese_lines:
                issues[filepath] = chinese_lines
            total_files_scanned += 1
    else:
        for root, dirs, files in os.walk(repo_root):
            # Skip hidden directories and common non-essential dirs
            dirs[:] = [d for d in dirs if not d.startswith('.') and d not in {'node_modules', 'vendor', '__pycache__'}]

            for filename in files:
                filepath = os.path.join(root, filename)
                rel_path = os.path.relpath(filepath, repo_root)

                if not should_scan(rel_path):
                    continue

                chinese_lines = find_chinese_in_file(filepath)
                if chinese_lines:
                    issues[rel_path] = chinese_lines
                total_files_scanned += 1

    print(f"Scanned {total_files_scanned} file(s)")
    return issues


def print_report(issues: dict):
    """Print formatted report of Chinese character findings."""
    if not issues:
        print("\n" + "=" * 60)
        print(" PASS: No Chinese characters found in scanned files")
        print("=" * 60)
        return

    total_issues = sum(len(lines) for lines in issues.values())

    print("\n" + "=" * 60)
    print(f" FAIL: Found {total_issues} line(s) with Chinese characters in {len(issues)} file(s)")
    print("=" * 60)

    for filepath, lines in sorted(issues.items()):
        print(f"\n  File: {filepath}")
        print(f"  {'─' * 50}")
        for line_num, snippet, chars in lines[:5]:  # Show max 5 lines per file
            print(f"    Line {line_num}: Found Chinese '{''.join(chars[:5])}'")
            if snippet:
                print(f"      Context: {snippet}")

        if len(lines) > 5:
            print(f"    ... and {len(lines) - 5} more lines")

    print("\n" + "=" * 60)
    print(" ACTION REQUIRED: Remove Chinese characters before committing")
    print("=" * 60)
    print("\n  Quick fix - Remove all Chinese from a file:")
    print("    python .github/scripts/check_chinese.py --remove <filepath>")
    print("  Or manually translate to English and re-commit.")
    print()


def main():
    repo_root = os.getcwd()
    files_to_check = None
    remove_mode = False
    remove_file = None

    # Parse arguments
    args = sys.argv[1:]
    if args:
        if '--remove' in args:
            idx = args.index('--remove')
            if idx + 1 < len(args):
                remove_file = args[idx + 1]
                remove_mode = True
                args = args[:idx] + args[idx + 2:]
            else:
                print("Error: --remove requires a file path argument")
                sys.exit(1)

        if args:
            files_to_check = args

    # Remove mode - strip Chinese from a specific file
    if remove_mode and remove_file:
        filepath = os.path.join(repo_root, remove_file)
        if not os.path.isfile(filepath):
            print(f"Error: File not found: {remove_file}")
            sys.exit(1)

        chinese_pattern = re.compile(r'[\u4e00-\u9fff]')

        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        new_content = chinese_pattern.sub('', content)

        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)

        print(f"Removed all Chinese characters from: {remove_file}")
        sys.exit(0)

    # Scan mode
    issues = scan_repo(repo_root, files_to_check)
    print_report(issues)

    # Exit with non-zero if issues found
    if issues:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == '__main__':
    main()
