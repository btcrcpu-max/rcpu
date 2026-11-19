# Security Policy

## Reporting a Vulnerability

To report security issues send an email to **rcpudevs@proton.me**.

If you are sending from Protonmail, your email will already be end-to-end encrypted.

If sending from a non-Protonmail account, encrypt with the following key:

| Name | Fingerprint |
|------|-------------|
| RCPU Dev Team (rcpudevs@proton.me) | `934D 5BC9 5DD4 B3AC FEF5 21B9 5476 3350 1FE4 B8EE` |

The armored public key is committed to the repository at `RCPU-DEV-GPG-KEY.asc`.
Release artifacts are signed with this key: the node binaries are listed in
`SHA256SUMS.txt`, which ships with the detached GPG signature
`SHA256SUMS.txt.asc`. The GUI AppImage is not part of `SHA256SUMS.txt`; it
carries its own `rcpu-qt-<version>-x86_64.AppImage.sha256` and a detached
`rcpu-qt-<version>-x86_64.AppImage.asc` over that checksum file.

Artifact signatures and tag signatures are separate guarantees. Release tags
are expected to be signed with the same key, but an artifact signature only
proves that the checksum file was produced by the release key. Verify both
before trusting a release:

```
gpg --verify SHA256SUMS.txt.asc SHA256SUMS.txt
git verify-tag v<version>
```

A release whose tag is unsigned is still checksum-signed, but its provenance
rests on the GitHub account that pushed the tag rather than on the tag
signature.
