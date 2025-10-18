# 快速开始指南

## 5 分钟快速部署

### 方法 1: Docker（推荐）

```bash
# 1. 克隆仓库
git clone https://github.com/yourorg/opcua-http-bridge.git
cd opcua-http-bridge

# 2. 创建配置文件
cp examples/config_template.env .env

# 3. 编辑配置（修改 OPC UA 服务器地址）
nano .env  # Linux
notepad .env  # Windows

# 4. 启动服务
docker-compose up -d

# 5. 验证运行
curl http://localhost:3000/health
```

### 方法 2: 二进制文件

#### Windows

```powershell
# 1. 下载最新版本
# 从 GitHub Releases 下载 opcua2http.exe

# 2. 创建工作目录
mkdir C:\opcua-http-bridge
cd C:\opcua-http-bridge

# 3. 复制文件
copy opcua2http.exe .

# 4. 创建配置文件
copy examples\config_template.env .env
notepad .env

# 5. 运行服务
.\opcua2http.exe
```

#### Linux

```bash
# 1. 下载最新版本
wget https://github.com/yourorg/opcua-http-bridge/releases/latest/opcua2http-linux.tar.gz
tar -xzf opcua2http-linux.tar.gz

# 2. 进入目录
cd opcua-http-bridge

# 3. 创建配置文件
cp examples/config_template.env .env
nano .env

# 4. 运行服务
chmod +x opcua2http
./opcua2http
```

## 基本配置

最小配置（编辑 `.env` 文件）：

```bash
# OPC UA 服务器地址
OPC_ENDPOINT=opc.tcp://192.168.1.100:4840

# HTTP 服务端口
SERVER_PORT=3000

# 安全模式（开发环境）
OPC_SECURITY_MODE=1
OPC_SECURITY_POLICY=None
```

## 测试连接

### 1. 检查服务健康

```bash
curl http://localhost:3000/health
```

期望响应：
```json
{
  "service": "opcua-http-bridge",
  "status": "running",
  "opc_connected": true,
  "cached_items": 0,
  "active_subscriptions": 0
}
```

### 2. 读取 OPC UA 数据

```bash
# 读取单个节点
curl "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature"

# 读取多个节点
curl "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature,ns=2;s=Pressure"
```

期望响应：
```json
{
  "readResults": [
    {
      "id": "ns=2;s=Temperature",
      "s": true,
      "r": "Good",
      "v": "25.5",
      "t": 1678886400000
    }
  ]
}
```

## 常见问题

### 无法连接到 OPC UA 服务器

```bash
# 检查网络连接
ping 192.168.1.100
telnet 192.168.1.100 4840

# 查看详细日志
opcua2http.exe --debug
```

### 端口已被占用

```bash
# 修改端口
SERVER_PORT=8080

# 或检查占用端口的进程
netstat -ano | findstr :3000  # Windows
lsof -i :3000  # Linux
```

### 认证失败

```bash
# 添加 API Key
curl -H "X-API-Key: your_api_key" http://localhost:3000/health

# 或使用 Basic Auth
curl -u admin:password http://localhost:3000/health
```

## 下一步

- 📖 阅读 [README.md](README.md) 了解完整 API 文档
- 🚀 查看 [DEPLOYMENT.md](DEPLOYMENT.md) 了解生产部署
- 🔧 参考 [MAINTENANCE.md](MAINTENANCE.md) 了解维护指南
- ⚙️ 查看 [examples/config_template.env](examples/config_template.env) 了解所有配置选项

## 生产环境建议

在生产环境部署前，请确保：

1. ✅ 使用加密连接（`OPC_SECURITY_MODE=3`）
2. ✅ 配置认证（`API_KEY` 或 `AUTH_USERNAME/PASSWORD`）
3. ✅ 限制 CORS 源（`ALLOWED_ORIGINS`）
4. ✅ 设置合适的日志级别（`LOG_LEVEL=info`）
5. ✅ 配置自动重启（systemd/Windows Service/Docker）
6. ✅ 设置监控和告警
7. ✅ 定期备份配置文件

详细信息请参考 [DEPLOYMENT.md](DEPLOYMENT.md)。
