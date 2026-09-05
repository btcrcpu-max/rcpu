// Copyright (c) 2015 Gregory Maxwell
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/confidential.h>

#include <crypto/common.h>

bool g_con_elementsmode = false;
thread_local bool g_ct_serialization = false;

void CConfidentialValue::SetToAmount(const CAmount amount)
{
    vchCommitment.resize(nExplicitSize);
    vchCommitment[0] = 1;
    WriteBE64(&vchCommitment[1], amount);
}
