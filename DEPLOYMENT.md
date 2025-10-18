# OPC UA HTTP Bridge - Deployment Guide

## Table of Contents
- [Overview](#overview)
- [System Requirements](#system-requirements)
- [Installation Methods](#installation-methods)
- [Configuration](#configuration)
- [Docker Deployment](#docker-deployment)
- [Production Deployment](#production-deployment)
- [Basic Monitoring](#basic-monitoring)
- [Troubleshooting](#troubleshooting)
- [Security Best Practices](#security-best-practices)

## Overview

The OPC UA HTTP Bridge is a high-performance gateway that connects OPC UA servers to HTTP/REST clients. It features intelligent caching, automatic subscription management, and robust reconnection handling.

### Key Features
- **Smart Caching**: Automatic subscription-based caching for optimal performance
- **Auto-Reconnection**: Resilient connection management with exponential backoff
- **Thread-Safe**: Concurrent request handling with read-write locks
- **Flexible Authentication**: API Key and Basic Auth support
- **CORS Support**: Configurable cross-origin resource sharing

## System Requirements

### Minimum Requirements
- **OS**: Windows 10/11, Linux (Ubuntu 20.04+), or Docker
- **CPU**: 2 cores
- **RAM**: 512 MB
- **Disk**: 100 MB free space
- **Network**: Access to OPC UA server (typically port 4840)

### Recommended Requirements
- **CPU**: 4+ cores
- **RAM**: 2 GB
- **Network**: Low-latency connection to OPC UA server (<10ms)

### Dependencies
- **Runtime**: C++ Runtime (MSVC 2019+ on Windows, GCC 9+ on Linux)
- **OPC UA Server**: Compatible open62541 server
- **Network**: TCP/IP connectivity

## Installation Methods

### Method 1: Binary Installation (Recommended for Production)

#### Windows
```bash
# Download the latest release
# Extract to installation directory
mkdir C:\opcua-http-bridge
cd C:\opcua-http-bridge

# Copy binary and configuration
copy opcua2http.exe .
copy config_template.env .env

# Edit configuration
notepad .env

# Run the service
opcua2http.exe
```

#### Linux
```bash
# Download and extract
wget https://github.com/yourorg/opcua-http-bridge/releases/latest/opcua2http-linux.tar.gz
tar -xzf opcua2http-linux.tar.gz
cd opcua-http-bridge

# Make executable
chmod +x opcua2http

# Copy and edit configuration
cp config_template.env .env
nano .env

# Run the service
./opcua2http
```

### Method 2: Build from Source

#### Prerequisites
```bash
# Install CMake (3.20+)
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # Linux/Mac
./bootstrap-vcpkg.bat # Windows
```

#### Build Steps
```bash
# Clone repository
git clone https://github.com/yourorg/opcua-http-bridge.git
cd opcua-http-bridge

# Configure with vcpkg
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release --target opcua2http

# Binary location
# Windows: build/Release/opcua2http.exe
# Linux: build/opcua2http
```

### Method 3: Docker Deployment (Recommended for Containers)

See [Docker Deployment](#docker-deployment) section below.

## Configuration

### Environment Variables

Create a `.env` file or set system environment variables:

```bash
# === Core OPC UA Configuration ===
OPC_ENDPOINT=opc.tcp://192.168.1.100:4840
OPC_SECURITY_MODE=1                        # 1:None, 2:Sign, 3:SignAndEncrypt
OPC_SECURITY_POLICY=None                   # None, Basic256Sha256, Aes128_Sha256_RsaOaep
OPC_NAMESPACE=2                            # Default namespace
OPC_APPLICATION_URI=urn:opcua-http-bridge

# === Connection Management ===
CONNECTION_RETRY_MAX=5                     # Retries per attempt
CONNECTION_INITIAL_DELAY=1000              # Initial delay (ms)
CONNECTION_MAX_RETRY=-1                    # Global max retries (-1 = infinite)
CONNECTION_MAX_DELAY=30000                 # Max delay between retries (ms)
CONNECTION_RETRY_DELAY=5000                # Base retry delay (ms)

# === Web Server ===
SERVER_PORT=3000                           # HTTP listen port

# === Security ===
API_KEY=your_secure_api_key_here          # X-API-Key header value
AUTH_USERNAME=admin                        # Basic auth username
AUTH_PASSWORD=your_secure_password         # Basic auth password
ALLOWED_ORIGINS=*                          # CORS origins (comma-separated)

# === Cache Configuration ===
CACHE_EXPIRE_MINUTES=60                    # Cache entry expiration
SUBSCRIPTION_CLEANUP_MINUTES=30            # Unused subscription cleanup

# === Logging ===
LOG_LEVEL=info                             # trace, debug, info, warn, error, critical
```

### Configuration Validation

Verify your configuration:
```bash
opcua2http.exe --config
```

This displays all active configuration values and validates settings.

### Security Modes Explained

| Mode | Value | Description | Use Case |
|------|-------|-------------|----------|
| None | 1 | No encryption | Development, trusted networks |
| Sign | 2 | Message signing | Data integrity verification |
| SignAndEncrypt | 3 | Full encryption | Production, untrusted networks |

### Security Policies

- **None**: No security (use with SecurityMode=1 only)
- **Basic256Sha256**: Strong encryption, widely supported
- **Aes128_Sha256_RsaOaep**: Modern encryption standard
- **Aes256_Sha256_RsaPss**: Highest security level

## Docker Deployment

### Quick Start with Docker

```bash
# Build image
docker build -t opcua-http-bridge:latest .

# Run container
docker run -d \
  --name opcua-bridge \
  -p 3000:3000 \
  -e OPC_ENDPOINT=opc.tcp://host.docker.internal:4840 \
  -e API_KEY=your_api_key \
  opcua-http-bridge:latest
```

### Docker Compose (Recommended)

Create `docker-compose.yml`:
```yaml
version: '3.8'

services:
  opcua-bridge:
    image: opcua-http-bridge:latest
    container_name: opcua-http-bridge
    ports:
      - "3000:3000"
    environment:
      - OPC_ENDPOINT=opc.tcp://opcua-server:4840
      - OPC_SECURITY_MODE=1
      - SERVER_PORT=3000
      - API_KEY=${API_KEY}
      - AUTH_USERNAME=${AUTH_USERNAME}
      - AUTH_PASSWORD=${AUTH_PASSWORD}
    env_file:
      - .env
    restart: unless-stopped
    networks:
      - opcua-network
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:3000/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 40s

networks:
  opcua-network:
    driver: bridge
```

Start services:
```bash
docker-compose up -d
```

### Docker Health Checks

Monitor container health:
```bash
# Check status
docker ps

# View logs
docker logs opcua-bridge

# Check health endpoint
curl http://localhost:3000/health
```

## Production Deployment

### Windows Service Installation

#### Using NSSM (Non-Sucking Service Manager)

```powershell
# Download NSSM from https://nssm.cc/download

# Install service
nssm install OPCUABridge "C:\opcua-http-bridge\opcua2http.exe"

# Configure service
nssm set OPCUABridge AppDirectory "C:\opcua-http-bridge"
nssm set OPCUABridge DisplayName "OPC UA HTTP Bridge"
nssm set OPCUABridge Description "OPC UA to HTTP Gateway Service"
nssm set OPCUABridge Start SERVICE_AUTO_START

# Set environment variables
nssm set OPCUABridge AppEnvironmentExtra OPC_ENDPOINT=opc.tcp://192.168.1.100:4840
nssm set OPCUABridge AppEnvironmentExtra SERVER_PORT=3000

# Start service
nssm start OPCUABridge

# Check status
nssm status OPCUABridge
```

### Linux Systemd Service

Create `/etc/systemd/system/opcua-bridge.service`:

```ini
[Unit]
Description=OPC UA HTTP Bridge
After=network.target

[Service]
Type=simple
User=opcua
Group=opcua
WorkingDirectory=/opt/opcua-http-bridge
EnvironmentFile=/opt/opcua-http-bridge/.env
ExecStart=/opt/opcua-http-bridge/opcua2http
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/opt/opcua-http-bridge/logs

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
# Create user
sudo useradd -r -s /bin/false opcua

# Set permissions
sudo chown -R opcua:opcua /opt/opcua-http-bridge

# Enable service
sudo systemctl daemon-reload
sudo systemctl enable opcua-bridge
sudo systemctl start opcua-bridge

# Check status
sudo systemctl status opcua-bridge

# View logs
sudo journalctl -u opcua-bridge -f
```

## Basic Monitoring

### Health Check Endpoint

检查服务状态：

```bash
curl http://localhost:3000/health
```

响应示例：
```json
{
  "service": "opcua-http-bridge",
  "status": "running",
  "uptime_seconds": 3600,
  "opc_connected": true,
  "cached_items": 25,
  "active_subscriptions": 25
}
```

### 日志级别

- **trace**: 详细调试信息
- **debug**: 开发和故障排查
- **info**: 正常操作（默认）
- **warn**: 警告信息
- **error**: 错误信息
- **critical**: 严重故障

设置日志级别：
```bash
LOG_LEVEL=debug opcua2http.exe
```

### 配置备份

```bash
# 备份配置
tar -czf opcua-bridge-config-$(date +%Y%m%d).tar.gz .env

# 恢复配置
tar -xzf opcua-bridge-config-20240101.tar.gz
```

## Troubleshooting

### Common Issues

#### 1. Cannot Connect to OPC UA Server

**Symptoms**: Service starts but health check shows `opc_connected: false`

**Solutions**:
```bash
# Verify endpoint is reachable
ping opcua-server-hostname

# Check port accessibility
telnet opcua-server-hostname 4840

# Verify security settings match server
opcua2http.exe --config

# Check logs for detailed error
opcua2http.exe --debug
```

#### 2. Authentication Failures

**Symptoms**: HTTP 401 Unauthorized responses

**Solutions**:
```bash
# Verify API key
curl -H "X-API-Key: your_key" http://localhost:3000/health

# Test basic auth
curl -u username:password http://localhost:3000/health

# Check environment variables
opcua2http.exe --config | grep AUTH
```

#### 3. High Memory Usage

**Symptoms**: Memory consumption grows over time

**Solutions**:
```bash
# Reduce cache expiration
CACHE_EXPIRE_MINUTES=30

# Increase cleanup frequency
SUBSCRIPTION_CLEANUP_MINUTES=15

# Monitor cache size
curl http://localhost:3000/health | grep cached_items
```

#### 4. Slow Response Times

**Symptoms**: API requests take >1 second

**Solutions**:
```bash
# Check OPC UA server latency
# Verify network connection quality

# Increase cache hit rate by reducing expiration
CACHE_EXPIRE_MINUTES=120

# Check if subscriptions are active
curl http://localhost:3000/health | grep active_subscriptions
```

#### 5. Service Crashes on Startup

**Symptoms**: Service exits immediately

**Solutions**:
```bash
# Run with debug logging
opcua2http.exe --debug

# Verify configuration
opcua2http.exe --config

# Check for missing dependencies (Windows)
# Use Dependency Walker or similar tool

# Check system logs
# Windows: Event Viewer
# Linux: journalctl -xe
```

### Debug Mode

Enable detailed logging:
```bash
# Command line
opcua2http.exe --debug

# Or set environment variable
LOG_LEVEL=debug
opcua2http.exe
```

### Getting Help

1. **Check logs**: Enable debug logging first
2. **Verify configuration**: Use `--config` flag
3. **Test connectivity**: Verify OPC UA server access
4. **Review documentation**: Check this guide and README.md
5. **Report issues**: Include logs, configuration (sanitized), and error messages

## Security Best Practices

### 1. Authentication

```bash
# Always use strong API keys (32+ characters)
API_KEY=$(openssl rand -base64 32)

# Use complex passwords
AUTH_PASSWORD=$(openssl rand -base64 24)

# Rotate credentials regularly (every 90 days)
```

### 2. Network Security

```bash
# Restrict CORS origins
ALLOWED_ORIGINS=https://trusted-domain.com,https://app.example.com

# Use HTTPS in production (via reverse proxy)
# Never expose directly to internet without TLS

# Firewall rules (Linux)
sudo ufw allow from 192.168.1.0/24 to any port 3000
sudo ufw deny 3000
```

### 3. OPC UA Security

```bash
# Use encryption in production
OPC_SECURITY_MODE=3
OPC_SECURITY_POLICY=Basic256Sha256

# Verify server certificates
# Configure certificate trust store
```

### 4. System Hardening

```bash
# Run as non-privileged user
# Never run as root/Administrator

# Limit file permissions
chmod 600 .env
chmod 700 opcua2http

# Use SELinux/AppArmor (Linux)
# Enable Windows Defender (Windows)
```

### 5. Monitoring and Auditing

```bash
# Enable audit logging
LOG_LEVEL=info

# Monitor failed authentication attempts
# Set up alerts for suspicious activity

# Regular security updates
# Keep dependencies updated
```

### 6. Data Protection

```bash
# Encrypt sensitive configuration
# Use secrets management (Vault, AWS Secrets Manager)

# Backup encryption keys
# Implement key rotation

# Secure log files
chmod 640 /var/log/opcua-bridge/*.log
```

## Appendix

### A. Environment Variable Reference

See [Configuration](#configuration) section for complete list.

### B. API Reference

See [README.md](README.md) for API documentation.

### C. Performance Benchmarks

Expected performance on typical hardware (4 cores, 8GB RAM):
- **Throughput**: 1000+ requests/second
- **Latency**: <50ms (cached), <200ms (uncached)
- **Concurrent connections**: 100+
- **Cache capacity**: 10,000+ items
- **Uptime**: 99.9%+ with auto-reconnection

### D. Version History

- **v1.0.0**: Initial release with core functionality
- **v1.1.0**: Added Docker support and health checks
- **v1.2.0**: Enhanced monitoring and logging

### E. License

See LICENSE file for details.

### F. Support

- **Documentation**: README.md, DEPLOYMENT.md
- **Issues**: GitHub Issues
- **Email**: fly@zynfly.wang
