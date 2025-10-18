# Testing Best Practices

## 🚨 CRITICAL RULE: Always Compile Before Testing

**MANDATORY: Tests must be compiled before running**

```bash
# ALWAYS compile first
cmake --build cmake-build-debug --target opcua2http_tests

# THEN run tests
cmake-build-debug/Debug/opcua2http_tests.exe
```

## Core Principle: Test Isolation

**Each test MUST be completely independent**

- ✅ Single test runs → passes
- ✅ All tests together → pass
- ✅ Any test order → same results

## Common Pitfalls & Solutions

### 1. Global State Pollution - spdlog

```cpp
// ❌ WRONG: Returns destroyed object
void TearDown() override {
    spdlog::set_default_logger(spdlog::default_logger());
}

// ✅ CORRECT: Save and restore
std::shared_ptr<spdlog::logger> original_logger;

void SetUp() override {
    original_logger = spdlog::default_logger();
    // ... setup test logger
}

void TearDown() override {
    if (original_logger) {
        spdlog::set_default_logger(original_logger);
    } else {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", sink));
    }
}
```

### 2. Shared Resource Race Conditions

```cpp
// ✅ CORRECT: Use mutex for shared MockServer
static std::mutex setupMutex_;

void SetUp() override {
    std::lock_guard<std::mutex> lock(setupMutex_);
    mockServer_->updateTestVariable(1001, value);
}

void TearDown() override {
    std::lock_guard<std::mutex> lock(setupMutex_);
    // Cleanup with delay for async operations
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
```

### 3. Async Resource Cleanup

```cpp
// ✅ CORRECT: Wait for async operations
void TearDown() override {
    client_.reset();
    // OPC UA connections need time to close
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
```

### 4. Component Dependencies

```cpp
// ✅ CORRECT: Reset global state explicitly
void SetUp() override {
    // Ensure clean spdlog state
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    spdlog::set_default_logger(logger);
    
    bridge_ = std::make_unique<OPCUAHTTPBridge>();
    bridge_->initialize();
}
```

## Quick Debugging Guide

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| Pass alone, fail together | Global state pollution | Check spdlog, env vars, statics |
| SEH 0xc0000005 | Accessing destroyed object | Check logger, shared_ptr lifetime |
| Instant fail (0ms) | SetUp crash | Add logs, check init order |
| Intermittent failures | Race condition | Add mutex, increase delays |

### Debug Commands

```bash
# Run single test
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="TestName.*"

# Run test with previous tests
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="PrevTest.*:FailTest.*"
```

## Key Lessons from This Project

1. **spdlog pollution**: Always save/restore original logger
2. **Shared MockServer**: Use mutex to serialize access
3. **Async cleanup**: Add 200ms delay in TearDown
4. **Global state**: Reset explicitly in isolated tests (e.g., EndToEnd)

**Remember: Good tests are independent universes - they never interfere with each other.**
