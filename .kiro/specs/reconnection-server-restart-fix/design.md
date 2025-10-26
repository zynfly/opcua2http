# Design Document - 设计文档

## Overview - 概述

This design addresses the bug where ReconnectionManager fails to reconnect after an OPC UA server restart. The root cause analysis reveals that the current implementation only checks `isConnected()` status but doesn't actively attempt reconnection when the server becomes available again after being shut down.

本设计解决 ReconnectionManager 在 OPC UA 服务器重启后无法重连的 bug。根本原因分析表明，当前实现只检查 `isConnected()` 状态，但在服务器关闭后再次可用时不会主动尝试重连。

### Root Cause Analysis - 根本原因分析

**Critical Issue: Missing `runIterate()` Call**
**关键问题：缺少 `runIterate()` 调用**

**Verified with Context7 Documentation:**
**通过 Context7 文档验证：**

- `UA_Client_runAsync()` or `UA_Client_run_iterate()` must be called regularly to process network events
- 必须定期调用 `UA_Client_runAsync()` 或 `UA_Client_run_iterate()` 来处理网络事件
- Without these calls, the client cannot detect connection state changes
- 没有这些调用，客户端无法检测连接状态变化
- The `stateCallback` in `UA_ClientConfig` is only triggered when events are processed
- `UA_ClientConfig` 中的 `stateCallback` 仅在处理事件时触发

**Primary Problem**: The `monitoringLoop()` in ReconnectionManager does NOT call `opcClient_->runIterate()`:
**主要问题**：ReconnectionManager 中的 `monitoringLoop()` 没有调用 `opcClient_->runIterate()`：

```cpp
void ReconnectionManager::monitoringLoop() {
    while (monitoring_.load()) {
        bool isConnected = checkConnectionStatus();  // Only checks state
        // MISSING: opcClient_->runIterate()  // <-- This is needed!

        if (connectionLost && !isConnected) {
            attemptReconnection();
        }

        std::this_thread::sleep_for(monitorInterval);
    }
}
```

**Consequences of missing `runIterate()`**:
**缺少 `runIterate()` 的后果**：

1. **Delayed disconnection detection**: Client doesn't process network events, so it doesn't know the server is down until much later
   **延迟的断开检测**：客户端不处理网络事件，因此直到很久之后才知道服务器已关闭

2. **Missed state callbacks**: The `stateCallback` is never triggered because events aren't processed
   **错过状态回调**：因为事件未被处理，`stateCallback` 永远不会被触发

3. **Stale connection state**: `isConnected()` returns stale information
   **过时的连接状态**：`isConnected()` 返回过时的信息

4. **Failed reconnection**: Even when server restarts, the client doesn't detect it's available
   **重连失败**：即使服务器重启，客户端也检测不到它已可用

**Secondary Problem**: After reaching max retries, the system waits too long before trying again:
**次要问题**：达到最大重试次数后，系统等待太久才再次尝试：

```cpp
if (!hasReachedMaxRetries()) {
    // Wait and retry
} else {
    // PROBLEM: Waits 2*maxDelay before reset
    // 问题：重置前等待 2*maxDelay
    auto longDelay = std::chrono::milliseconds(connectionMaxDelay_ * 2);
    waitOrStop(longDelay);
}
```

## Architecture - 架构

### Component Interaction - 组件交互

```
┌─────────────────────┐
│ ReconnectionManager │
│  monitoringLoop()   │
└──────────┬──────────┘
           │
           ├─ checkConnectionStatus()
           │  └─> OPCUAClient::isConnected()
           │
           ├─ attemptReconnection()
           │  └─> OPCUAClient::connect()
           │
           └─ recoverSubscriptions()
              └─> SubscriptionManager::recreateAllMonitoredItems()
```

### State Machine - 状态机

Current problematic state flow:
当前有问题的状态流：

```
MONITORING → Connection Lost → RECONNECTING
    ↓
Retry 1, 2, 3... → Max Retries Reached
    ↓
Long Wait (2 * maxDelay) → Reset Counter
    ↓
MONITORING (but server might be up already!)
```

Improved state flow:
改进后的状态流：

```
MONITORING → Connection Lost → RECONNECTING
    ↓
Continuous retry with backoff
    ↓
Server becomes available → Reconnect Success
    ↓
RECOVERING_SUBSCRIPTIONS → MONITORING
```

## Components and Interfaces - 组件和接口

### 1. ReconnectionManager Modifications - ReconnectionManager 修改

#### Changes to `monitoringLoop()` - 对 `monitoringLoop()` 的修改

**Primary Fix**: Add `opcClient_->runIterate()` call to process network events:
**主要修复**：添加 `opcClient_->runIterate()` 调用来处理网络事件：

```cpp
void ReconnectionManager::monitoringLoop() {
    while (monitoring_.load()) {
        // CRITICAL: Process network events to detect state changes
        // 关键：处理网络事件以检测状态变化
        opcClient_->runIterate(10);  // 10ms timeout

        bool isConnected = checkConnectionStatus();

        if (connectionLost && !isConnected) {
            attemptReconnection();
        }

        std::this_thread::sleep_for(monitorInterval);
    }
}
```

**Secondary Fix**: Improve retry strategy to continue attempting connection:
**次要修复**：改进重试策略以继续尝试连接：

- Remove the long wait after max retries
- 移除达到最大重试次数后的长时间等待
- Continue retrying with capped delay
- 使用上限延迟继续重试

#### Actual Retry Strategy (As Implemented) - 实际重试策略（已实现）

```cpp
Phase 1: Normal Retries with Exponential Backoff (指数退避重试阶段)
- Attempts: 1 to connectionMaxRetry_ (default: 5)
- Delay: Exponential backoff (connectionRetryDelay_ * 2^(attempt-1))
- Max delay: connectionMaxDelay_ (default: 10s)
- Example: 500ms → 1s → 2s → 4s → 8s

Phase 2: Reset and Retry (重置并重试阶段)
- After reaching connectionMaxRetry_ attempts
- Wait: 2 * connectionMaxDelay_ (default: 20s)
- Reset retry counter to 0
- Return to Phase 1
- Continue indefinitely until connection succeeds
- 持续尝试直到连接成功
```

**Note**: This creates a gap in reconnection attempts after each retry cycle. The gap allows the system to avoid overwhelming the OPC UA server with continuous connection attempts, but it means there are periods (default: 20 seconds) where no reconnection attempts are made.

**注意**：这会在每个重试周期后创建一个重连尝试的间隔。该间隔可以避免用持续的连接尝试压垮 OPC UA 服务器，但这意味着存在不进行重连尝试的时间段（默认：20 秒）。

**Future Enhancement Consideration**: Implement continuous retries with capped delay (no reset) for scenarios where quick reconnection is critical and server load is not a concern.

**未来增强考虑**：为快速重连至关重要且服务器负载不是问题的场景实现带上限延迟的持续重试（无重置）。

### 2. Test Infrastructure - 测试基础设施

#### MockServer Enhancements - MockServer 增强

Need to add server lifecycle control:
需要添加服务器生命周期控制：

```cpp
class MockServer {
public:
    void start();           // Start server
    void stop();            // Stop server (simulate shutdown)
    void restart();         // Stop then start (simulate restart)
    bool isRunning();       // Check if server is running
};
```

#### Test Scenarios - 测试场景

1. **Server Shutdown Test** - 服务器关闭测试
   - Start with connected client
   - Stop server
   - Verify disconnection detected within 2 seconds

2. **Server Restart Test** - 服务器重启测试
   - Start with connected client
   - Stop server
   - Wait for disconnection detection
   - Restart server
   - Verify reconnection within 10 seconds
   - Verify subscriptions restored

3. **Multiple Restart Test** - 多次重启测试
   - Perform multiple restart cycles
   - Verify successful reconnection each time

## Data Models - 数据模型

### ReconnectionStats Enhancement - ReconnectionStats 增强

Add new fields to track extended retry behavior:
添加新字段来跟踪扩展重试行为：

```cpp
struct ReconnectionStats {
    // Existing fields...

    // New fields
    uint64_t extendedRetryAttempts;      // Attempts after max retries
    bool inExtendedRetryMode;            // Whether in extended retry mode
    std::chrono::milliseconds currentRetryDelay; // Current retry delay
};
```

## Error Handling - 错误处理

### Connection Failure Scenarios - 连接失败场景

1. **Server Down** - 服务器关闭
   - `UA_Client_connect()` returns error immediately
   - Continue retrying with backoff

2. **Network Issue** - 网络问题
   - Connection timeout
   - Continue retrying with backoff

3. **Server Restart** - 服务器重启
   - Initial connection attempts fail
   - Server becomes available
   - Next attempt succeeds
   - **Critical**: Must not stop trying before server is back
   - **关键**：在服务器恢复之前不能停止尝试

### Logging Strategy - 日志策略

Enhanced logging for debugging:
增强的调试日志：

```cpp
// Connection state changes
"Connection lost detected"
"Attempting reconnection (attempt X of Y)"
"Reconnection failed, waiting Zms"
"Entering extended retry mode"  // NEW
"Server became available, reconnection successful"

// Timing information
"Time since disconnection: Xms"
"Next retry in: Yms"
```

## Testing Strategy - 测试策略

### Unit Tests - 单元测试

1. **ReconnectionManager Logic Tests** - ReconnectionManager 逻辑测试
   - Test retry counter behavior
   - Test delay calculation
   - Test state transitions

### Integration Tests - 集成测试

1. **Server Shutdown Test** - 服务器关闭测试
   ```cpp
   TEST(ReconnectionIntegration, ServerShutdown) {
       // 1. Connect to server
       // 2. Stop server
       // 3. Verify disconnection detected within 2s
       // 4. Verify reconnection attempts continue
   }
   ```

2. **Server Restart Test** - 服务器重启测试
   ```cpp
   TEST(ReconnectionIntegration, ServerRestart) {
       // 1. Connect to server with subscriptions
       // 2. Stop server
       // 3. Wait 3 seconds
       // 4. Restart server
       // 5. Verify reconnection within 10s
       // 6. Verify subscriptions restored
   }
   ```

3. **Multiple Restart Test** - 多次重启测试
   ```cpp
   TEST(ReconnectionIntegration, MultipleRestarts) {
       // Perform 3 restart cycles
       // Verify successful reconnection each time
   }
   ```

### Test Timing Requirements - 测试时间要求

- Disconnection detection: < 2 seconds（断开检测：< 2 秒）
- Server availability detection: < 5 seconds（服务器可用检测：< 5 秒）
- Total reconnection time: < 10 seconds（总重连时间：< 10 秒）

## Implementation Phases - 实现阶段

### Phase 1: Fix Reconnection Logic - 修复重连逻辑
- Remove the "stop after max retries" behavior
- Implement continuous retry with capped delay
- 移除"达到最大重试次数后停止"的行为
- 实现带延迟上限的持续重试

### Phase 2: Add Test Infrastructure - 添加测试基础设施
- Add MockServer lifecycle control methods
- Create test utilities for timing verification
- 添加 MockServer 生命周期控制方法
- 创建时间验证的测试工具

### Phase 3: Implement Tests - 实现测试
- Write server shutdown test
- Write server restart test
- Write multiple restart test
- 编写服务器关闭测试
- 编写服务器重启测试
- 编写多次重启测试

### Phase 4: Enhance Logging - 增强日志
- Add extended retry mode logging
- Add timing information
- Improve diagnostic messages
- 添加扩展重试模式日志
- 添加时间信息
- 改进诊断消息

## Performance Considerations - 性能考虑

### Retry Timing - 重试时间

- Initial retries: Fast (exponential backoff)（初始重试：快速（指数退避））
- Extended retries: Slower (fixed at maxDelay)（扩展重试：较慢（固定在 maxDelay））
- Balance between responsiveness and resource usage（在响应性和资源使用之间平衡）

### Resource Usage - 资源使用

- Monitoring thread: Minimal CPU usage（监控线程：最小 CPU 使用）
- Connection attempts: Network I/O only（连接尝试：仅网络 I/O）
- No busy-waiting: Use sleep between attempts（无忙等待：尝试之间使用 sleep）

## Security Considerations - 安全考虑

No security implications for this bug fix. The reconnection mechanism uses the same security configuration as the initial connection.

此 bug 修复没有安全影响。重连机制使用与初始连接相同的安全配置。

## Backward Compatibility - 向后兼容性

This fix improves existing behavior without breaking changes:
此修复改进现有行为而不破坏兼容性：

- Configuration parameters remain the same（配置参数保持不变）
- API interfaces unchanged（API 接口不变）
- Only internal retry logic is modified（仅修改内部重试逻辑）

## Success Criteria - 成功标准

1. All new tests pass（所有新测试通过）
2. Server restart reconnection works reliably（服务器重启重连可靠工作）
3. Reconnection timing meets requirements (< 10s)（重连时间满足要求（< 10 秒））
4. No regression in existing reconnection behavior（现有重连行为无回归）
5. Detailed logging helps diagnose issues（详细日志帮助诊断问题）
