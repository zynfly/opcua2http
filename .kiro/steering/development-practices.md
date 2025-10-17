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
