# Development Practices and Problem-Solving Guidelines

## Using Context7 for Library Documentation

When working with external libraries (especially C/C++ libraries like open62541), always leverage Context7 to get accurate, up-to-date documentation:

### When to Use Context7

1. **Before implementing features** - Check library documentation for correct API usage
2. **When encountering errors** - Look up proper function signatures and parameter types
3. **For best practices** - Understand recommended patterns and common pitfalls
4. **When debugging** - Verify assumptions about library behavior

### How to Use Context7

```
Step 1: Resolve library ID
- Use mcp_Context7_resolve_library_id with the library name
- Example: "open62541", "crow", "nlohmann-json"

Step 2: Get documentation
- Use mcp_Context7_get_library_docs with the resolved library ID
- Specify a topic if you need focused information
- Example topics: "variable nodes", "server configuration", "data types"
```

### Example Workflow

When implementing OPC UA variable nodes:
1. Query Context7 for "open62541" documentation on "variable nodes"
2. Review the proper way to create variables with correct type definitions
3. Implement based on official documentation patterns
4. Test and verify

## Deep Analysis Approach

When encountering problems, **always perform deep analysis** before attempting fixes:

### Problem Analysis Framework

#### 1. Understand the Symptom
- What is the exact error message or unexpected behavior?
- When does it occur? (initialization, runtime, specific conditions)
- What is the expected vs actual behavior?

#### 2. Investigate the Root Cause
- Don't just fix the surface symptom
- Ask "why" multiple times to get to the root cause
- Example from our OPC UA work:
  - Symptom: "No value available" when reading nodes
  - Surface cause: Client can't read the value
  - Deeper cause: Server has value but client read fails
  - Root cause: Variable type definition was NULL, preventing proper value storage/retrieval

#### 3. Verify Assumptions
- Check library documentation (use Context7!)
- Add diagnostic logging to understand internal state
- Test each component in isolation
- Example: Read value directly from server to verify it exists before testing client read

#### 4. Systematic Debugging
- Add verification steps at each stage
- Log intermediate states
- Compare working vs non-working scenarios
- Example: Server-side verification showed value was stored correctly, narrowing problem to client-side reading

#### 5. Implement Complete Solutions
- Fix the root cause, not just symptoms
- Set all required attributes/parameters
- Example: Setting complete variable attributes including:
  - `dataType` - explicit type definition
  - `valueRank` - scalar vs array
  - `accessLevel` - read/write permissions
  - `variableType` - proper type node reference

### Red Flags That Require Deep Analysis

- **Repeated failures** with the same approach
- **Inconsistent behavior** (works sometimes, fails others)
- **Partial success** (some features work, others don't)
- **Workarounds needed** (if you need hacks, something is wrong)
- **Documentation conflicts** (your code differs from examples)

### Analysis Tools and Techniques

1. **Add diagnostic output**
   - Log function entry/exit
   - Print intermediate values
   - Show state transitions

2. **Isolate components**
   - Test server independently
   - Test client independently
   - Test integration last

3. **Compare with references**
   - Check official examples
   - Review library test cases
   - Consult Context7 documentation

4. **Verify at each layer**
   - Data creation (is it correct?)
   - Data storage (is it persisted?)
   - Data retrieval (can we read it back?)
   - Data transmission (does it reach the client?)

## Lessons Learned from OPC UA Implementation

### Variable Node Creation
**Problem**: Variables created but values not readable by clients

**Root Cause**: Using `UA_NODEID_NULL` for variable type and incomplete attributes

**Solution**: 
- Use `UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE)` for variable type
- Set complete attributes: `dataType`, `valueRank`, `accessLevel`, `userAccessLevel`
- Verify server-side storage before testing client reads

**Key Insight**: The library requires explicit type definitions and complete attribute sets for proper functionality. Don't rely on defaults.

### Testing Strategy
**Approach**: Build verification into tests
- Create mock server with diagnostic output
- Verify data at source before testing retrieval
- Add server-side read verification to confirm storage
- Only then test client-side reads

**Key Insight**: Multi-layer verification helps isolate problems quickly.

## Best Practices Summary

1. **Use Context7 first** - Don't guess API usage, look it up
2. **Analyze deeply** - Find root causes, not just symptoms
3. **Verify assumptions** - Test each layer independently
4. **Document insights** - Record what you learn for future reference
5. **Complete implementations** - Set all required parameters, not just the obvious ones
6. **Test systematically** - Build verification into your tests
7. **Learn from failures** - Each problem teaches proper usage patterns

## When Stuck

If you've tried the same approach 2-3 times without success:

1. **STOP** - Don't keep trying the same thing
2. **Use Context7** - Look up the official documentation
3. **Analyze deeply** - What are you assuming that might be wrong?
4. **Ask for help** - Explain the problem and what you've tried
5. **Try a different approach** - Based on documentation, not guesses

Remember: **Time spent on deep analysis is time saved on repeated failures.**

## Common C++ Library Integration Issues

### Open62541 OPC UA Library Specific Issues

#### API Structure Understanding
- **Problem**: Assuming field locations in structures without verification
- **Solution**: Always check Context7 documentation for exact structure definitions
- **Example**: `clientHandle` is in `UA_MonitoringParameters`, not `UA_MonitoredItemCreateResult`

#### Callback Function Signatures
- **Problem**: Incorrect callback parameter types leading to compilation errors
- **Solution**: Verify callback signatures in official documentation
- **Example**: Status change callbacks receive `UA_StatusChangeNotification*`, not `UA_StatusCode`

#### API Initialization Patterns
- **Problem**: Using function pointers instead of function calls for default initialization
- **Solution**: Use `UA_CreateSubscriptionRequest_default()` not `UA_CreateSubscriptionRequest_default`

### Thread Safety and Concurrency

#### Mutex Deadlock Prevention
- **Problem**: Calling public thread-safe methods from within locked contexts
- **Solution**: Create separate internal "unsafe" methods for use within locked contexts
- **Pattern**: 
  ```cpp
  // Public thread-safe method
  void updateLastAccessed(const std::string& nodeId) {
      std::lock_guard<std::mutex> lock(mutex_);
      updateLastAccessedUnsafe(nodeId);
  }
  
  // Private unsafe method for internal use
  void updateLastAccessedUnsafe(const std::string& nodeId) {
      // Implementation without locking
  }
  ```

### Testing Framework Issues

#### Multiple Main Functions
- **Problem**: Each test file having its own main() function causes linking conflicts
- **Solution**: Only one test file should have main(), others should just define test cases
- **Best Practice**: Use a common test runner or let the testing framework handle main()

#### Mock Server Lifecycle
- **Problem**: Mock servers not properly cleaned up between tests
- **Solution**: Ensure proper setup/teardown in test fixtures with adequate wait times

### Build System Integration

#### CMake Library Linking
- **Problem**: Forgetting to add new source files to CMakeLists.txt
- **Solution**: Always update both SOURCES and TEST_SOURCES when adding new components
- **Check**: Verify compilation succeeds after adding new files

### Debugging Strategies for C++ Library Integration

1. **Start with Context7**: Always check official documentation before implementing
2. **Verify Structures**: Use debugger or logging to verify structure contents
3. **Test Incrementally**: Build and test each component separately
4. **Check Signatures**: Verify function and callback signatures match exactly
5. **Thread Safety**: Be explicit about which methods are thread-safe
6. **Resource Management**: Ensure proper cleanup in destructors and error paths

## OPC UA Development Best Practices

### Before Starting OPC UA Implementation

1. **Always Use Context7 First**
   - Query Context7 for the specific OPC UA library (open62541) before writing code
   - Focus searches on specific topics: "subscriptions", "monitored items", "callbacks"
   - Verify API signatures and structure definitions

2. **Understand the OPC UA Concepts**
   - **Subscriptions**: Container for monitored items with publishing intervals
   - **Monitored Items**: Individual data points being watched for changes
   - **Client Handles**: User-defined identifiers for tracking monitored items
   - **Server Handles**: Server-assigned identifiers returned in responses

### Open62541 Specific Patterns

#### Subscription Creation Pattern
```cpp
// Correct pattern for creating subscriptions
UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
request.requestedPublishingInterval = 1000.0;
request.requestedLifetimeCount = 10000;
request.requestedMaxKeepAliveCount = 10;

UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(
    client, request, context, statusCallback, inactivityCallback);
```

#### Monitored Item Creation Pattern
```cpp
// Correct pattern for creating monitored items
UA_MonitoredItemCreateRequest request;
UA_MonitoredItemCreateRequest_init(&request);
request.itemToMonitor.nodeId = nodeId;
request.itemToMonitor.attributeId = UA_ATTRIBUTEID_VALUE;
request.monitoringMode = UA_MONITORINGMODE_REPORTING;
request.requestedParameters.clientHandle = getNextClientHandle();
request.requestedParameters.samplingInterval = 1000.0;

UA_MonitoredItemCreateResult result = UA_Client_MonitoredItems_createDataChange(
    client, subscriptionId, UA_TIMESTAMPSTORETURN_BOTH, request, 
    context, dataChangeCallback, nullptr);
```

#### Callback Function Signatures
```cpp
// Data change notification callback
static void dataChangeNotificationCallback(
    UA_Client *client, UA_UInt32 subId, void *subContext, 
    UA_UInt32 monId, void *monContext, UA_DataValue *value);

// Status change notification callback  
static void subscriptionStatusChangeCallback(
    UA_Client *client, UA_UInt32 subId, void *subContext, 
    UA_StatusChangeNotification *notification);

// Subscription inactivity callback
static void subscriptionInactivityCallback(
    UA_Client *client, UA_UInt32 subId, void *subContext);
```

### Error Handling Patterns

#### Status Code Checking
```cpp
// Always check status codes from OPC UA operations
UA_StatusCode status = UA_Client_connect(client, endpoint.c_str());
if (status != UA_STATUSCODE_GOOD) {
    logError("Connection failed: " + std::string(UA_StatusCode_name(status)));
    return false;
}
```

#### Resource Cleanup
```cpp
// Always clean up OPC UA resources
UA_NodeId_clear(&nodeId);  // For allocated node IDs
UA_String_clear(&string);  // For allocated strings
UA_Variant_clear(&variant); // For variants
```

### Testing OPC UA Components

#### Mock Server Requirements
- Use proper variable type definitions with `UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE)`
- Set complete variable attributes: `dataType`, `valueRank`, `accessLevel`, `userAccessLevel`
- Use global variables to ensure lifetime during server operation
- Allow adequate time for server startup and connection establishment

#### Test Structure
- Separate test setup/teardown properly
- Clean up connections before stopping mock servers
- Use unique ports for each test to avoid conflicts
- Verify server-side data storage before testing client reads

### Performance Considerations

#### Subscription Management
- Use reasonable publishing intervals (1000ms is good for testing)
- Limit queue sizes to prevent memory issues
- Clean up unused subscriptions and monitored items
- Use client handles to efficiently map notifications to data

#### Thread Safety
- Protect all subscription and monitored item collections with mutexes
- Use read-write locks for better concurrent read performance
- Avoid calling public thread-safe methods from within locked contexts
- Implement separate internal unsafe methods for use within locks

### Common Pitfalls to Avoid

1. **Don't assume structure field locations** - Always verify with Context7
2. **Don't ignore status codes** - Check every OPC UA operation result
3. **Don't forget resource cleanup** - Use RAII patterns and proper destructors
4. **Don't mix thread-safe and unsafe calls** - Be explicit about locking requirements
5. **Don't skip server-side verification** - Test data storage before client reads
6. **Don't use default values blindly** - Set all required parameters explicitly