### Verify Binaries

#### Preparation

RCPU releases are signed with a single release key, fingerprint

```
934D 5BC9 5DD4 B3AC FEF5 21B9 5476 3350 1FE4 B8EE
```

This key is pinned in `verify.py` as `RELEASE_KEY_FINGERPRINT`. A checksum file
signed by any other key is rejected even if the signature itself is valid, so
there is no builder-key trust model to configure.

Import the release key once:

```sh
gpg --keyserver hkps://keys.openpgp.org --recv-keys 934D5BC95DD4B3ACFEF521B9547633501FE4B8EE
```

#### Usage

`verify.py pub <version>` downloads `SHA256SUMS.txt` and `SHA256SUMS.txt.asc`
from `https://github.com/btcrcpu-max/rcpu/releases/download/v<version>/`,
checks the signature against the pinned key, then downloads every file listed in
the checksum file and compares the hashes.

The script is fail-closed:

* a checksum file signed by an unexpected key, or with fewer than
  `--min-good-sigs` trusted signatures (default 1), is rejected;
* an explicitly `-unsigned` asset is rejected unless `--allow-unsigned` is
  given, because it means the Windows Authenticode signature is missing;
* a platform named by `--require-platforms` (default `linux,win`) must have at
  least one asset in the checksum file;
* a file listed in the checksum file but absent from the release is an error,
  not a warning.

Exit codes: 0 everything verified, 1 integrity failure, 2 usage error,
3+ download / GPG errors.

#### Examples

Validate a release with default settings:

```sh
./contrib/verify-binaries/verify.py pub 1.4.17
```

JSON output:

```sh
./contrib/verify-binaries/verify.py --json pub 1.4.17
```

Only download one platform:

```sh
./contrib/verify-binaries/verify.py pub 1.4.17-win64
```

Delete the downloaded files afterwards:

```sh
./contrib/verify-binaries/verify.py pub --cleanup 1.4.17
```

Verify the files listed in a local checksum file (requires the matching
`SHA256SUMS.txt.asc` next to it):

```sh
./contrib/verify-binaries/verify.py bin ~/Downloads/SHA256SUMS.txt
```

Tests (offline):

```sh
python3 contrib/verify-binaries/test.py
```
