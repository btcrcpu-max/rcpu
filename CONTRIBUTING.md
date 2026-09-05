# Contributing to RCPU Core

Thank you for your interest in contributing to RCPU Core!

## How to Contribute

1. **Fork** the repository at <https://github.com/btcrcpu-max/rcpu>
2. **Create a branch** from `main` for your feature or bugfix
3. **Build and test** locally before submitting:
   ```bash
   ./autogen.sh
   ./configure --without-gui --disable-bench
   make -j$(nproc)
   make check
   ```
4. **Open a Pull Request** against the `main` branch

## Guidelines

- Follow the existing code style (Bitcoin Core / C++ conventions)
- Include unit tests for new consensus-critical logic
- Document any changes to chain parameters in `doc/consensus-params.md`
- Do **not** modify the genesis block or coinbase string \u2014 changing these breaks chain compatibility
- Keep `pool/` out of the core repository; pool code lives in a separate repo

## Reporting Issues

Use the GitHub issue tracker: <https://github.com/btcrcpu-max/rcpu/issues>

## Security

For security disclosures, contact the maintainers via the GPG key listed in
`SECURITY.md`. Do **not** open public issues for security vulnerabilities.

