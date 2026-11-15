# Migrating a legacy wallet that holds confidential UTXOs

v1.4.11 stops a wallet without a confidential scriptPubKeyManager from
auto-selecting blinded UTXOs. This avoids the old failure where a legacy
wallet held a blinded coin but could not create the confidential change output
needed to spend it. The coins are not lost, but they are intentionally not
selectable from that wallet until it has confidential change capability.

## Recommended migration

1. Create a descriptor wallet (or upgrade the existing wallet through the
   normal wallet migration flow) and make sure it has confidential
   descriptors. On current releases this is the default for a newly created
   descriptor wallet.
2. Import the relevant keys/descriptors into the descriptor wallet and rescan
   the chain from the birth height of the legacy wallet.
3. Confirm the blinded balance is visible and spendable in the descriptor
   wallet before deleting the old wallet.
4. Keep the legacy wallet backup until the migrated wallet has confirmed the
   funds and completed at least one spend.

## Why the legacy wallet cannot do this itself

Creating a valid transaction from a blinded input to a plain `rcpu1...`
recipient requires a blinded change output to balance the Pedersen commitment.
A legacy/imported-key wallet has no confidential descriptor manager, so it
cannot derive that change destination. Selecting the input manually is also
rejected with an explicit error rather than producing an unspendable
transaction.

## Release-note wording

This migration note is referenced by the v1.4.12 release notes. The wallet
error is intentionally fail-closed; it is not a consensus failure and does not
affect the 27,326 strict-nonce activation height.
