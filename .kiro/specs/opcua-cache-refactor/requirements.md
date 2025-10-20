# Requirements Document

## Introduction

本项目需要对现有的OPC UA到HTTP网关进行重构，将基于订阅的数据获取机制改为基于缓存的读取机制。新系统将实现智能缓存策略：数据缓存10秒有效期，但超过3秒后允许重新读取以获取最新数据。这种改进将解决部分OPC UA服务器不支持订阅导致数据不更新的问题，同时保持高性能的数据访问。

## Glossary

- **Cache_System**: 缓存系统，管理OPC UA数据的本地存储和有效期
- **OPC_Client**: OPC UA客户端，负责与OPC UA服务器通信
- **HTTP_Gateway**: HTTP网关服务，提供REST API接口
- **Read_Strategy**: 读取策略，决定何时从缓存返回数据或重新读取OPC UA服务器

## Requirements

### Requirement 1

**User Story:** 作为Web应用开发者，我希望通过/iotgateway/read API获取OPC UA数据时，系统能够智能使用缓存机制提供快速响应，同时确保数据的时效性。

#### Acceptance Criteria

1. WHEN Web客户端发送GET请求到/iotgateway/read?ids=<node-ids>, THE Cache_System SHALL 返回JSON格式的readResults数组
2. WHEN 请求的数据点在缓存中且未超过3秒, THE Cache_System SHALL 直接返回缓存数据
3. WHEN 请求的数据点在缓存中但超过3秒且未超过10秒, THE Cache_System SHALL 从OPC UA服务器重新读取数据并更新缓存
4. WHEN 请求的数据点缓存已超过10秒或不存在, THE Cache_System SHALL 从OPC UA服务器读取数据并创建新的缓存条目

### Requirement 2

**User Story:** 作为系统管理员，我希望缓存系统能够自动管理数据的生命周期，避免内存泄漏和过期数据的累积。

#### Acceptance Criteria

1. WHEN 缓存条目创建时, THE Cache_System SHALL 记录数据值、时间戳和创建时间
2. WHEN 缓存条目超过10秒, THE Cache_System SHALL 标记该条目为过期状态
3. WHEN 系统进行缓存清理时, THE Cache_System SHALL 删除所有超过10秒的过期条目
4. WHILE 系统运行, THE Cache_System SHALL 每60秒自动执行一次缓存清理操作

### Requirement 3

**User Story:** 作为Web应用开发者，我希望系统能够高效处理批量数据请求，对不同缓存状态的数据点采用最优的读取策略。

#### Acceptance Criteria

1. WHEN 批量请求包含多个NodeId, THE Read_Strategy SHALL 分别评估每个数据点的缓存状态
2. WHEN 批量请求中有数据点需要重新读取, THE OPC_Client SHALL 使用批量读取操作从OPC UA服务器获取数据
3. WHEN 批量读取操作完成, THE Cache_System SHALL 同时更新所有相关缓存条目
4. WHEN 并发请求访问相同数据点, THE Cache_System SHALL 使用线程安全机制防止重复读取

### Requirement 4

**User Story:** 作为系统集成商，我希望新的缓存机制完全兼容现有的API接口和配置系统，无需修改客户端代码。

#### Acceptance Criteria

1. THE HTTP_Gateway SHALL 保持现有的/iotgateway/read API端点完全不变
2. WHEN API返回数据, THE HTTP_Gateway SHALL 使用标准JSON响应格式（nodeId、success、quality、value、timestamp_iso字段）
3. WHEN 客户端发送请求, THE HTTP_Gateway SHALL 接受完全相同的查询参数格式（ids参数）
4. WHEN 配置系统参数, THE Cache_System SHALL 支持通过环境变量配置缓存时间参数
5. THE HTTP_Gateway SHALL 保持现有的认证和CORS配置机制完全不变
6. THE HTTP_Gateway SHALL 保持现有的错误响应格式和HTTP状态码不变

### Requirement 5

**User Story:** 作为运维人员，我希望系统在OPC UA连接异常时能够优雅处理，提供清晰的错误信息和自动恢复机制。

#### Acceptance Criteria

1. WHEN OPC UA服务器连接失败, THE OPC_Client SHALL 返回连接错误状态给Cache_System
2. IF 连接错误发生且缓存中有有效数据, THEN THE Cache_System SHALL 返回缓存数据并标记为连接错误状态
3. IF 连接错误发生且缓存中无数据, THEN THE HTTP_Gateway SHALL 返回包含错误信息的JSON响应
4. WHEN OPC UA连接恢复, THE OPC_Client SHALL 自动恢复正常的数据读取操作

### Requirement 6

**User Story:** 作为性能监控人员，我希望系统提供缓存命中率和读取性能的监控信息，便于优化系统配置。

#### Acceptance Criteria

1. WHEN 数据请求处理完成, THE Cache_System SHALL 记录缓存命中或未命中的统计信息
2. WHILE 系统运行, THE Cache_System SHALL 维护缓存命中率、总请求数和平均响应时间统计
3. WHEN 查询系统状态, THE HTTP_Gateway SHALL 提供缓存统计信息的API端点
4. IF 缓存性能异常, THEN THE Cache_System SHALL 记录警告日志并触发清理操作

### Requirement 7

**User Story:** 作为Web应用开发者，我希望系统能够处理高并发请求，确保在多个客户端同时访问时的数据一致性和性能。

#### Acceptance Criteria

1. WHEN 多个并发请求访问相同NodeId, THE Cache_System SHALL 确保只有一个线程执行OPC UA读取操作
2. WHILE 并发读取操作进行中, THE Cache_System SHALL 让其他请求等待读取完成后共享结果
3. WHEN 缓存更新操作执行, THE Cache_System SHALL 使用原子操作确保数据一致性
4. WHILE 系统负载较高, THE Cache_System SHALL 优先使用缓存数据减少OPC UA服务器压力

### Requirement 8

**User Story:** 作为系统管理员，我希望能够通过配置参数调整缓存策略，以适应不同的应用场景和性能要求。

#### Acceptance Criteria

1. WHERE 缓存刷新阈值配置, THE Cache_System SHALL 支持通过CACHE_REFRESH_THRESHOLD_SECONDS环境变量设置（默认3秒）
2. WHERE 缓存过期时间配置, THE Cache_System SHALL 支持通过CACHE_EXPIRE_SECONDS环境变量设置（默认10秒）
3. WHERE 缓存清理间隔配置, THE Cache_System SHALL 支持通过CACHE_CLEANUP_INTERVAL_SECONDS环境变量设置（默认60秒）
4. IF 配置参数无效或缺失, THEN THE Cache_System SHALL 使用合理的默认值并记录配置警告

### Requirement 9

**User Story:** 作为性能工程师，我希望系统使用最优的并发控制机制，最小化锁竞争以提高系统吞吐量和响应速度。

#### Acceptance Criteria

1. WHEN 实现缓存数据结构, THE Cache_System SHALL 优先使用原子操作（std::atomic）而非互斥锁处理简单状态变更
2. WHEN 需要保护共享数据, THE Cache_System SHALL 使用读写锁（std::shared_mutex）替代独占锁以支持并发读取
3. WHEN 访问缓存条目, THE Cache_System SHALL 使用无锁数据结构或最小粒度锁减少锁竞争
4. WHEN 清理现有锁机制, THE Cache_System SHALL 移除不必要的互斥锁并用更高效的同步原语替代
