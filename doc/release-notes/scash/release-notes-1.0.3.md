1.0.3 Release Notes
===================

RCPU is built from source. There are no binaries available yet.

RCPU versioning is as follows:
```
RCPU version v1.x.x-mithril-core-26.0.0 
              |        |            |
            RCPU   CODENAME    BITCOIN CORE
```

Please report bugs using the issue tracker at GitHub:

  <https://github.com/rcpu-project/rcpu/issues>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases) then just copy over
`rcpud` (on Linux).

Compatibility
=============

RCPU is built as a new chain type on top of the Bitcoin Core software. RCPU
can connect to the Bitcoin network and operate as a Bitcoin client fully compatible
with the current network consensus rules. However, it is not recommended to use RCPU
as a Bitcoin client, and instead Bitcoin Core should be used.

Changes
=======

- QT GUI app updated to support the RCPU chain
- Checkpoint added

