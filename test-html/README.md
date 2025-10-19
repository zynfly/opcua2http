# OPC UA HTTP Bridge - 离线测试工具

这个目录包含了用于测试OPC UA HTTP Bridge的离线HTML工具。

## 文件结构

```
test-html/
├── assets/
│   ├── alpine.min.js      # Alpine.js v3.15.0 (本地版本)
│   └── tailwind.min.js    # Tailwind CSS v4 (本地版本)
├── opcua-test.html        # 主测试页面 (离线版本)
└── README.md              # 说明文档
```

## 功能特性

### 🔧 配置管理
- **API端点配置**: 可自定义OPC UA HTTP Bridge的地址
- **动态Node ID管理**: 
  - 添加/删除多个OPC UA节点ID
  - 每个节点ID独立输入框
  - 支持标准OPC UA节点ID格式 (如: `ns=25;i=59524`)

### 🧪 测试功能
- **健康检查**: 测试 `/health` 端点
- **单节点测试**: 测试第一个节点ID
- **多节点测试**: 测试所有配置的节点ID
- **自动刷新**: 每秒自动获取实时数值

### 📊 实时监控
- **实时数值显示**: 
  - 显示节点ID、当前值、质量状态
  - 成功/失败状态指示器 (✓/✗)
  - 时间戳显示
- **自动刷新倒计时**: 显示下次更新时间
- **响应式布局**: 支持桌面和移动设备

### 📝 测试结果
- **详细日志**: 记录所有API请求和响应
- **错误处理**: 显示CORS错误和网络错误详情
- **JSON查看器**: 可展开查看完整响应数据

## 使用方法

### 1. 启动OPC UA HTTP Bridge
确保你的OPC UA HTTP Bridge服务正在运行，默认地址为 `http://localhost:8080`

### 2. 打开测试页面
直接在浏览器中打开 `opcua-test.html` 文件

### 3. 配置测试参数
- **API端点**: 修改为你的Bridge服务地址
- **Node IDs**: 
  - 默认包含3个示例节点ID
  - 点击 "Remove" 删除不需要的节点
  - 点击 "+ Add Node ID" 添加新节点

### 4. 执行测试
- **Test Health Endpoint**: 检查服务状态
- **Test First Node**: 测试第一个节点
- **Test All Nodes (X)**: 测试所有节点 (X为节点数量)
- **Start Auto Refresh**: 开始实时监控

## 离线优势

✅ **完全离线**: 无需互联网连接  
✅ **快速加载**: 本地资源，无CDN依赖  
✅ **稳定可靠**: 不受外部服务影响  
✅ **版本固定**: 使用经过测试的库版本  

## 支持的响应格式

工具自动识别以下JSON响应格式：

```json
{
  "readResults": [
    {
      "nodeId": "ns=25;i=59524",
      "value": "5",
      "quality": "good",
      "success": true,
      "timestamp_iso": "2025-10-19T09:29:01.608Z"
    }
  ]
}
```

## 技术栈

- **Alpine.js v3.15.0**: 轻量级JavaScript框架
- **Tailwind CSS v4**: 实用优先的CSS框架
- **原生Fetch API**: HTTP请求处理
- **响应式设计**: 支持各种屏幕尺寸

## 故障排除

### CORS错误
如果遇到CORS错误，请确保OPC UA HTTP Bridge配置了正确的CORS头：
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

### 连接失败
- 检查Bridge服务是否运行
- 验证API端点地址是否正确
- 确认防火墙设置允许连接

### 节点读取失败
- 验证节点ID格式是否正确
- 检查OPC UA服务器连接状态
- 确认节点在OPC UA服务器中存在

## 更新资源

如需更新Alpine.js或Tailwind CSS版本：

```bash
# 更新Alpine.js
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/alpinejs@3.15.0/dist/cdn.min.js" -OutFile "assets/alpine.min.js"

# 更新Tailwind CSS
Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4" -OutFile "assets/tailwind.min.js"
```