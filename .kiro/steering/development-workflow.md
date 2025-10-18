# Development Workflow

## 🚨 CRITICAL RULE: Compile-Test-Fix Cycle

**MANDATORY WORKFLOW: Always follow this exact sequence:**

1. **COMPILE** - Build the project and check for compilation errors
2. **TEST** - Run tests to identify failures  
3. **FIX FIRST ERROR ONLY** - Deep analysis and fix only the first failing test
4. **REPEAT** - Go back to step 1 until all tests pass

### Deep Analysis Requirements

**🚨 NEVER fix surface symptoms - always find the ROOT CAUSE**

#### Analysis Process:
1. **Understand the Symptom** - What is the exact error message?
2. **Investigate Root Cause** - Why is this happening? Don't just fix what's visible
3. **Verify Assumptions** - Check library documentation with Context7 FIRST
4. **Implement Complete Solution** - Fix the underlying issue, not just the symptom

#### Context7 First Rule:
**BEFORE writing ANY code using external libraries:**
- Query Context7 for official documentation
- Verify API signatures and usage patterns
- Never guess or assume library behavior

### Example Workflow:

```bash
# 1. COMPILE
cmake --build cmake-build-debug --target opcua2http_tests

# 2. TEST (run first failing test only)
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="FirstFailingTest.*"

# 3. ANALYZE & FIX
# - Read error message carefully
# - Use Context7 to verify library usage
# - Find root cause (not just symptom)
# - Fix only this one issue

# 4. REPEAT until all tests pass
```

### Red Flags - Stop and Analyze Deeper:
- Same error repeating after "fixes"
- Need workarounds or hacks
- Code differs from official examples
- Multiple attempts at same API call

### Success Criteria:
- All tests pass
- No compiler warnings
- Code follows official library patterns
- Root causes addressed, not symptoms

**Remember: One error at a time, deep analysis always, Context7 before coding.**