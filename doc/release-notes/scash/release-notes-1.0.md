1.0 Release Notes
==================

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
==============

RCPU is built as a new chain type on top of the Bitcoin Core software. RCPU
can connect to the Bitcoin network and operate as a Bitcoin client fully compatible with the current network consensus rules. However, it is not recommended to use RCPU
as a Bitcoin client, and instead Bitcoin Core should be used.

Notable changes
===============

Proof of work
-------------
- The SHA256 proof of work has been replaced with RandomX.  See the the [RCPU Protocol spec](https://github.com/rcpu-project/sips/blob/main/rcpu-protocol-spec.md).

Replace-by-fee
-------------- 
- Disabled when running the RCPU network

Datacarrier
------------
- Disabled when running the RCPU network

Ordinals
--------
- Transactions containing ordinals inscriptions are treated as non-standard when running the RCPU network.

New options
-----------

- New chain options `-rcpu`, `-rcputestnet`, `-rcpuregtest`

- New proof of work related options `-randomxfastmode` and `-randomxvmcachesize`.
  See the [RCPU Protocol spec](https://github.com/rcpu-project/sips/blob/main/rcpu-protocol-spec.md).

Updated RPCs
------------

- `getblock` RPC returns new fields `rx_cm`, `rx_hash`, `rx_epoch`

- `getblocktemplate` RPC returns new field `rx_epoch_duration`
