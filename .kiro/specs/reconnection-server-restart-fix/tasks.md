# Implementation Plan - 实施计划

## Overview - 概述

This implementation plan addresses the reconnection bug where the system fails to reconnect after an OPC UA server restart. The core fix (adding runIterate() to monitoringLoop) has been completed. Remaining tasks focus on consolidating tests and adding comprehensive test coverage.

本实施计划解决服务器重启后无法重连的 bug。核心修复（在 monitoringLoop 中添加 runIterate()）已完成。剩余任务专注于整合测试和添加全面的测试覆盖。

## Completed Tasks - 已完成任务

- [x] 1. Add MockServer lifecycle control
  - Implement `stop()`, `restart()`, `isRunning()` methods in MockServer
  - Enable server shutdown and restart simulation for testing
  - _Requirements: 2.1, 2.2_

- [x] 2. Write initial test to understand the problem
  - Create integration test: connect → stop server → restart server
  - Test currently PASSES because we manually call runIterate() in test
  - This reveals the root cause: monitoringLoop() doesn't call runIterate()
  - Include timing measurements (disconnection < 10s, reconnection < 10s)
  - _Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3, 2.5_

- [x] 3. Add runIterate() to monitoringLoop() [CRITICAL FIX - COMPLETED]
  - Modified `monitoringLoop()` in ReconnectionManager.cpp
  - Added `opcClient_->runIterate(10)` call at the beginning of the loop
  - This allows the client to process network events and detect disconnections
  - All existing tests now pass with this fix
  - _Requirements: 1.1, 1.2, 1.3_

## Remaining Tasks - 剩余任务

- [x] 4. Consolidate and refactor test files
  - Merge `test_reconnection_server_restart.cpp` and `test_reconnection_without_runiterate.cpp` into single file
  - Remove redundant test cases (both files test similar scenarios)
  - Keep the cleaner test structure and better naming
  - Ensure all tests rely on monitoringLoop() (no manual runIterate() calls)
  - _Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3, 2.5_

- [x] 5. Add comprehensive reconnection test scenarios
  - Test 1: Basic server restart (single cycle)
  - Test 2: Multiple server restart cycles (3-5 times)
  - Test 3: Server restart with short downtime (< 2s)
  - Test 4: Server restart with long downtime (> 10s)
  - Test 5: Verify subscription recovery after reconnection
  - Test 6: Verify reconnection statistics accuracy
  - _Requirements: 1.4, 1.5, 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 6. Add edge case and stress tests
  - Test rapid server restarts (restart immediately after reconnection)
  - Test reconnection during active data updates
  - Test behavior when server never comes back (verify continuous retry)
  - Test reconnection with different retry configurations
  - _Requirements: 1.5, 2.4, 3.1, 3.2_

- [x] 7. Verify fix and run regression tests
  - Ensure all new tests pass
  - Run all existing ReconnectionManager tests
  - Run all existing integration tests
  - Verify no performance degradation
  - Document test results
  - _Requirements: All_
