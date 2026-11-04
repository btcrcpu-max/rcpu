# Security Policy

## Reporting a Vulnerability

To report security issues send an email to **rcpudevs@proton.me**.

If you are sending from Protonmail, your email will already be end-to-end encrypted.

If sending from a non-Protonmail account, encrypt with the following key:

| Name | Fingerprint |
|------|-------------|
| RCPU Dev Team (rcpudevs@proton.me) | `934D 5BC9 5DD4 B3AC FEF5 21B9 5476 3350 1FE4 B8EE` |

The armored public key is committed to the repository at `RCPU-DEV-GPG-KEY.asc`.
Each release is signed with this key; the node binaries are listed in
`SHA256SUMS.txt`, which ships with the detached GPG signature
`SHA256SUMS.txt.asc`, so they can be verified against the git tag before use.
The GUI AppImage is not part of `SHA256SUMS.txt`; it carries its own
`rcpu-qt-<version>-x86_64.AppImage.sha256` and a detached
`rcpu-qt-<version>-x86_64.AppImage.asc` over that checksum file.
