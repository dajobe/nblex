# Plan: Code and Documentation Updates for Strategy Alignment

## Overview

Update code and documentation to align with the lightweight correlation tool strategy. Most changes are documentation-only since the codebase already follows the export pattern rather than building alerting systems.

## Changes Required

### Documentation Updates

#### 1. AGENTS.md Updates

**File:** `AGENTS.md`

**Changes:**
- Line 5: Change "observability platform" → "lightweight correlation tool"
- Line 22: Change "alerts" → "webhooks" or "HTTP endpoints" 
- Line 43: Change "alerting systems" → "export to alerting systems (via webhooks)"

**Rationale:** Aligns with strategy positioning as a tool, not a platform, and reframes alerting as export capability.

#### 2. SPEC.md Updates

**File:** `SPEC.md`

**Changes:**
- Line 193: Change "Alert systems" → "Export to alerting systems"

**Rationale:** Consistent with reframing alerting as export capability, not built-in system.

### Code Review - No Changes Needed

#### CLI Code (`cli/main.c`)

**Status:** ✅ Already aligned

- HTTP output is correctly positioned as webhook export (`--output-url`)
- No alerting system commands or flags
- No server mode commands
- Output formats are: json, file, http, metrics (all export-oriented)

#### HTTP Output Code (`src/output/http_output.c`)

**Status:** ✅ Already aligned

- File header comment says "HTTP output handler for webhooks" - correct positioning
- Implementation is webhook/export focused, not alerting system
- No alerting-specific logic

#### Configuration Parsing

**Status:** ✅ Already aligned

- Configuration examples in SPEC.md and README.md already updated (removed alerts section)
- Code should handle webhook export configuration, not alerting configuration
- If config parsing code exists, it should only handle webhook URLs, not alert rules

## Implementation Steps

1. **Update AGENTS.md**
   - Change line 5: "observability platform" → "lightweight correlation tool"
   - Change line 22: "alerts" → "webhooks" or "HTTP endpoints"
   - Change line 43: "alerting systems" → "export to alerting systems (via webhooks)"

2. **Update SPEC.md**
   - Change line 193: "Alert systems" → "Export to alerting systems"

3. **Verify Code Alignment**
   - Review any configuration parsing code to ensure it doesn't parse alerting sections
   - Verify comments/documentation strings in code files don't reference "platform" or "alerting system"
   - Ensure HTTP output is consistently described as webhook export

## Files to Modify

1. `AGENTS.md` - 3 changes
2. `SPEC.md` - 1 change

## Files to Review (No Changes Expected)

1. `cli/main.c` - Verify no "platform" or "alerting" references in comments
2. `src/output/http_output.c` - Verify comments align with webhook export positioning
3. `src/core/config.c` - Verify no alerting configuration parsing (if exists)

## Success Criteria

- All documentation consistently uses "lightweight correlation tool" terminology
- All alerting references reframed as "export to alerting systems"
- Code comments align with strategy positioning
- No references to "platform" in user-facing documentation
- HTTP output consistently described as webhook export

## Notes

- The codebase is already well-aligned with the strategy
- HTTP output implementation is correctly positioned as webhook export
- No alerting system code exists to remove
- Changes are primarily documentation updates for consistency

