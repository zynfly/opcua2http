# Requirements Document - 需求文档

## Introduction - 简介

This feature addresses a critical bug in the reconnection mechanism where the system fails to detect disconnection in a timely manner when an OPC UA Server is shut down. The root cause is that the ReconnectionManager's monitoring loop does not call `opcClient_->runIterate()`, which is required for the OPC UA client to process network events and detect connection state changes.

Without calling `runIterate()`, the client cannot:
- Detect when the server has shut down
- Process incoming network events
- Update its internal connection state
- Trigger reconnection attempts promptly

This results in delayed or missed disconnection detection, preventing the system from reconnecting when the server becomes available again.

本功能修复重连机制中的一个关键 bug：当 OPC UA 服务器关闭时，系统无法及时检测到断开连接。根本原因是 ReconnectionManager 的监控循环没有调用 `opcClient_->runIterate()`，而这是 OPC UA 客户端处理网络事件和检测连接状态变化所必需的。

不调用 `runIterate()` 会导致客户端无法：
- 检测服务器何时关闭
- 处理传入的网络事件
- 更新其内部连接状态
- 及时触发重连尝试

这导致断开连接检测延迟或遗漏，阻止系统在服务器再次可用时重新连接。

## Glossary - 术语表

- **ReconnectionManager**: The component responsible for monitoring OPC UA connection status and automatically attempting reconnection when connection is lost（负责监控 OPC UA 连接状态并在连接丢失时自动尝试重连的组件）
- **OPCUAClient**: The client component that maintains the connection to the OPC UA server（维护与 OPC UA 服务器连接的客户端组件）
- **Server Restart**: The scenario where an OPC UA server is shut down completely and then started again（OPC UA 服务器完全关闭后再次启动的场景）
- **Connection Monitoring Loop**: The background thread that continuously checks connection status（持续检查连接状态的后台线程）
- **Reconnection Attempt**: An active attempt to re-establish connection to the OPC UA server（主动尝试重新建立与 OPC UA 服务器的连接）

## Requirements - 需求

### Requirement 1 - 需求 1：服务器重启后自动重连

**User Story - 用户故事:** As a system operator, I want the system to automatically reconnect when the OPC UA server is restarted, so that I don't need to manually restart the client application.

作为系统操作员，我希望当 OPC UA 服务器重启时系统能自动重连，这样我就不需要手动重启客户端应用程序。

#### Acceptance Criteria - 验收标准

1. WHEN the OPC UA server is shut down, THE ReconnectionManager SHALL detect the connection loss within 10 seconds
   （当 OPC UA 服务器关闭时，ReconnectionManager 应在 10 秒内检测到连接丢失）

2. WHEN the OPC UA server is restarted after being shut down, THE ReconnectionManager SHALL detect the server availability within 5 seconds
   （当 OPC UA 服务器关闭后重新启动时，ReconnectionManager 应在 5 秒内检测到服务器可用）

3. WHEN the OPC UA server becomes available after restart, THE ReconnectionManager SHALL successfully reconnect within 10 seconds
   （当 OPC UA 服务器重启后变为可用时，ReconnectionManager 应在 10 秒内成功重连）

4. WHEN reconnection succeeds after server restart, THE ReconnectionManager SHALL restore all active subscriptions
   （当服务器重启后重连成功时，ReconnectionManager 应恢复所有活动订阅）

5. WHERE the server is restarted multiple times, THE ReconnectionManager SHALL successfully reconnect each time without manual intervention
   （当服务器多次重启时，ReconnectionManager 应每次都能成功重连而无需手动干预）

### Requirement 2 - 需求 2：服务器重启场景的测试覆盖

**User Story - 用户故事:** As a developer, I want comprehensive test coverage for the server restart scenario, so that I can verify the reconnection behavior works correctly.

作为开发人员，我希望对服务器重启场景有全面的测试覆盖，以便验证重连行为是否正常工作。

#### Acceptance Criteria - 验收标准

1. THE test suite SHALL include a test that simulates complete server shutdown
   （测试套件应包含模拟完整服务器关闭的测试）

2. THE test suite SHALL include a test that simulates server restart (shutdown followed by startup)
   （测试套件应包含模拟服务器重启（关闭后启动）的测试）

3. THE test SHALL verify that reconnection occurs automatically after server restart
   （测试应验证服务器重启后自动发生重连）

4. THE test SHALL verify that subscriptions are restored after reconnection
   （测试应验证重连后订阅被恢复）

5. THE test SHALL measure and verify reconnection timing meets the 10-second requirement
   （测试应测量并验证重连时间满足 10 秒的要求）

### Requirement 3 - 需求 3：重连事件的详细日志

**User Story - 用户故事:** As a system administrator, I want detailed logging of reconnection events, so that I can diagnose connection issues in production.

作为系统管理员，我希望有重连事件的详细日志，以便在生产环境中诊断连接问题。

#### Acceptance Criteria - 验收标准

1. WHEN the server connection is lost, THE ReconnectionManager SHALL log the disconnection event with timestamp
   （当服务器连接丢失时，ReconnectionManager 应记录带时间戳的断开连接事件）

2. WHEN reconnection attempts are made, THE ReconnectionManager SHALL log each attempt with attempt number and delay
   （当进行重连尝试时，ReconnectionManager 应记录每次尝试的尝试次数和延迟）

3. WHEN reconnection succeeds, THE ReconnectionManager SHALL log the success event with total downtime
   （当重连成功时，ReconnectionManager 应记录成功事件及总停机时间）

4. WHEN subscription recovery occurs, THE ReconnectionManager SHALL log the number of subscriptions restored
   （当订阅恢复时，ReconnectionManager 应记录恢复的订阅数量）

5. WHERE detailed logging is enabled, THE ReconnectionManager SHALL log connection status checks periodically
   （当启用详细日志时，ReconnectionManager 应定期记录连接状态检查）
