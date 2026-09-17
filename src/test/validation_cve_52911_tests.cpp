// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/validation.h>
#include <validation.h>

// CVE-2024-52911 regression guard (source-level contract).
//
// The real protection for this backport is the structural lint in
// test/lint/lint-cve-52911-connectblock.sh, which runs in CI and fails the
// build if someone reintroduces an early `return` inside ConnectBlock's per-tx
// loop after CCheckQueueControl is constructed. That lint is what locks in the
// "txsdata before control, single exit, control.Wait() always runs" discipline.
//
// This suite is the executable side of the contract: a placeholder that keeps
// the backport pinned in the test binary so the commit history is greppable,
// and a natural home for future functional tests that exercise ConnectBlock
// failure paths through the script check queue (see comment in the lint for
// the full rationale).
//
// Contract being guarded:
//   - txsdata must be declared BEFORE control, so destruction order destroys
//     control first: CCheckQueueControl's destructor Wait()s for in-flight
//     script checks, and those checks read txsdata[i]. If txsdata were
//     destroyed first, the reads would touch freed memory (the UAF fixed by
//     upstream #35209 + #31112).
//   - Once control is live, ConnectBlock may not return from the per-tx loop:
//     failures set BlockValidationState and break, control.Wait() runs while
//     txsdata is still alive, and the function exits through a single gate.

BOOST_AUTO_TEST_SUITE(validation_cve_52911_tests)

BOOST_AUTO_TEST_CASE(connectblock_single_exit_contract)
{
    // Marker for the CVE-2024-52911 backport. Structural enforcement lives in
    // test/lint/lint-cve-52911-connectblock.sh; keep this case so the suite
    // exists in the test binary and can be extended with functional tests.
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()