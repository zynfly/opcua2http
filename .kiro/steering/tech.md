# Tech Stack Reference

## Core Technologies
- **CMake** + **vcpkg** - Build system and package management
- **C++20** - Language standard
- **open62541** - OPC UA client library
- **Crow** - HTTP web framework  
- **nlohmann-json** - JSON processing

## Essential Build Commands
```bash
# Build tests
cmake --build cmake-build-debug --target opcua2http_tests

# Run specific test
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="TestName.*"

# Run all tests
cmake-build-debug/Debug/opcua2http_tests.exe
```

## Key Libraries for Context7 Queries
- `/open62541/open62541` - OPC UA operations
- `/crowcpp/crow` - HTTP request handling
- `/nlohmann/json` - JSON serialization

**Always query Context7 before using these libraries!**