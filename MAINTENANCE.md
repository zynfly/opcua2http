# OPC UA HTTP Bridge - 维护指南

## 日常维护

### 检查服务状态

```bash
# 检查健康状态
curl http://localhost:3000/health

# Windows 服务状态
Get-Service OPCUABridge

# Linux 服务状态
systemctl status opcua-bridge

# Docker 容器状态
docker ps | grep opcua-bridge
```

### 查看日志

```bash
# 查看最近的日志
tail -f /var/log/opcua-bridge/opcua2http.log  # Linux
Get-Content C:\opcua-http-bridge\logs\opcua2http.log -Tail 50 -Wait  # Windows
docker logs opcua-bridge -f  # Docker

# 查看错误日志
grep -i error /var/log/opcua-bridge/opcua2http.log
```

### 重启服务

```bash
# Windows
Restart-Service OPCUABridge

# Linux
sudo systemctl restart opcua-bridge

# Docker
docker restart opcua-bridge
```

## 常见问题

### 1. 无法连接到 OPC UA 服务器

**检查步骤：**
```bash
# 测试网络连接
ping opcua-server-hostname
telnet opcua-server-hostname 4840

# 检查配置
opcua2http.exe --config

# 查看详细日志
opcua2http.exe --debug
```

**解决方案：**
- 确认 OPC UA 服务器正在运行
- 检查防火墙规则
- 验证 OPC_ENDPOINT 配置正确
- 确认安全模式和策略匹配

### 2. 认证失败

**检查步骤：**
```bash
# 测试 API Key
curl -H "X-API-Key: your_key" http://localhost:3000/health

# 测试 Basic Auth
curl -u username:password http://localhost:3000/health

# 查看配置
opcua2http.exe --config | grep AUTH
```

**解决方案：**
- 验证 API_KEY 环境变量
- 检查 AUTH_USERNAME 和 AUTH_PASSWORD
- 确认客户端发送正确的认证头

### 3. 内存使用过高

**检查步骤：**
```bash
# 检查缓存大小
curl http://localhost:3000/health | jq '.cached_items'

# 查看内存使用
ps aux | grep opcua2http  # Linux
Get-Process opcua2http | Select-Object WorkingSet  # Windows
```

**解决方案：**
```bash
# 减少缓存过期时间
CACHE_EXPIRE_MINUTES=30

# 增加清理频率
SUBSCRIPTION_CLEANUP_MINUTES=15

# 重启服务清空缓存
```

### 4. 响应速度慢

**检查步骤：**
```bash
# 测试响应时间
time curl "http://localhost:3000/iotgateway/read?ids=ns=2;s=Test"

# 检查缓存命中率
curl http://localhost:3000/health
```

**解决方案：**
- 检查 OPC UA 服务器性能
- 验证网络延迟
- 确认订阅正常工作
- 增加缓存过期时间

### 5. 服务启动失败

**检查步骤：**
```bash
# 运行调试模式
opcua2http.exe --debug

# 验证配置
opcua2http.exe --config

# 查看系统日志
# Windows: 事件查看器
# Linux: journalctl -xe
```

**解决方案：**
- 检查配置文件语法
- 验证端口未被占用
- 确认依赖库已安装
- 检查文件权限

## 性能优化

### 缓存优化

```bash
# 稳定数据：增加缓存时间
CACHE_EXPIRE_MINUTES=120

# 高负载系统：减少清理频率
SUBSCRIPTION_CLEANUP_MINUTES=60
```

### 连接优化

```bash
# 关键系统：快速重连
CONNECTION_RETRY_DELAY=2000
CONNECTION_MAX_DELAY=10000

# 更积极的重试
CONNECTION_RETRY_MAX=10
```

## 备份和恢复

### 备份配置

```bash
# 创建备份
tar -czf opcua-bridge-backup-$(date +%Y%m%d).tar.gz .env

# Windows
Compress-Archive -Path .env -DestinationPath "backup-$(Get-Date -Format 'yyyyMMdd').zip"
```

### 恢复配置

```bash
# 恢复备份
tar -xzf opcua-bridge-backup-20240101.tar.gz

# Windows
Expand-Archive -Path backup-20240101.zip -DestinationPath .
```

### 数据恢复

服务是无状态的，所有数据来自 OPC UA 服务器：
1. 服务重启后自动重连 OPC UA 服务器
2. 缓存在首次请求时重建
3. 订阅自动重新创建

## 安全维护

### 定期更新凭证

```bash
# 生成新的 API Key（每 90 天）
openssl rand -base64 32

# 生成新密码
openssl rand -base64 24

# 更新 .env 文件
nano .env  # Linux
notepad .env  # Windows
```

### 检查访问日志

```bash
# 查看认证失败
grep "Authentication failed" /var/log/opcua-bridge/opcua2http.log

# 查看可疑活动
grep -E "401|403" /var/log/opcua-bridge/opcua2http.log
```

### 更新安全配置

```bash
# 生产环境使用加密
OPC_SECURITY_MODE=3
OPC_SECURITY_POLICY=Basic256Sha256

# 限制 CORS 源
ALLOWED_ORIGINS=https://trusted-domain.com
```

## 监控建议

### 关键指标

| 指标 | 正常值 | 警告阈值 |
|------|--------|----------|
| 响应时间 | <100ms | >500ms |
| OPC 连接 | true | false |
| 缓存大小 | <1000 | >5000 |
| 内存使用 | <500MB | >800MB |
| CPU 使用 | <30% | >60% |

### 简单监控脚本

```bash
#!/bin/bash
# simple_monitor.sh

while true; do
    # 检查健康状态
    health=$(curl -s http://localhost:3000/health)
    status=$(echo "$health" | jq -r '.status')
    opc_connected=$(echo "$health" | jq -r '.opc_connected')
    
    if [ "$status" != "running" ] || [ "$opc_connected" != "true" ]; then
        echo "$(date): WARNING - Service unhealthy"
        # 发送告警（邮件、Slack 等）
    fi
    
    sleep 60
done
```

## 升级指南

### 升级前准备

1. 备份当前配置
2. 记录当前版本
3. 阅读更新日志
4. 在测试环境验证

### 升级步骤

```bash
# 1. 停止服务
systemctl stop opcua-bridge  # Linux
Stop-Service OPCUABridge  # Windows
docker stop opcua-bridge  # Docker

# 2. 备份
cp opcua2http opcua2http.backup
cp .env .env.backup

# 3. 替换二进制文件
cp opcua2http-new opcua2http

# 4. 检查配置兼容性
opcua2http --config

# 5. 启动服务
systemctl start opcua-bridge  # Linux
Start-Service OPCUABridge  # Windows
docker start opcua-bridge  # Docker

# 6. 验证
curl http://localhost:3000/health
```

### 回滚步骤

```bash
# 1. 停止服务
systemctl stop opcua-bridge

# 2. 恢复旧版本
cp opcua2http.backup opcua2http
cp .env.backup .env

# 3. 启动服务
systemctl start opcua-bridge

# 4. 验证
curl http://localhost:3000/health
```

## 联系支持

如果遇到无法解决的问题：

1. 收集以下信息：
   - 服务版本（`opcua2http --version`）
   - 配置信息（`opcua2http --config`，隐藏敏感信息）
   - 错误日志（最近 100 行）
   - 系统信息（操作系统、CPU、内存）

2. 查看文档：
   - README.md - 基本使用
   - DEPLOYMENT.md - 部署指南
   - 本文档 - 维护指南

3. 报告问题：
   - GitHub Issues
   - 技术支持邮箱
