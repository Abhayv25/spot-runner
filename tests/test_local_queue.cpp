// =============================================================================
// test_local_queue.cpp (PHASE 4)
// =============================================================================
// Same fixture idea as test_local_storage.cpp (fresh temp folder per test).
//
// TESTS TO WRITE:
//   1. EmptyQueueReturnsNothing: receive() == nullopt
//   2. PushThenReceive: fields match (job_id, image, env), receipt not empty
//   3. ReceivedJobIsHidden: push 1 job, receive it, a second receive() == nullopt
//   4. CompleteRemovesForever: complete, then wait past the lease; still nothing
//   5. ReleaseMakesItVisibleAgain: release(job, false) then receive() gets it, attempts 0
//   6. ReleaseAsFailureCountsAttempt: release(job, true) -> attempts == 1
//   7. ExpiredLeaseReturnsJob: receive with lease_seconds = 1, sleep 2 s,
//      receive again -> same job comes back (the "computer vanished" case!)
//   8. ExtendLeaseKeepsItHidden: lease 1 s, extend to 5 s, sleep 2 s, receive -> nothing
//   9. ExtendAfterLossThrows: let it expire and be taken again, then extend_lease on the
//      OLD job -> throws
//  10. TwoQueuesSameFolderNeverDoubleClaim (the cool one):
//      push 50 jobs; start 4 std::threads, each with its OWN LocalQueue on the same
//      folder, each receiving until empty and recording job_ids. Afterwards: every job
//      seen exactly once. This proves your rename trick works.
//  11. JsonRoundTrip: job_from_json(job_to_json(job)) gives the same fields.
// =============================================================================
