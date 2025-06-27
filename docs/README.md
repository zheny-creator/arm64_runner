# Livepatch System for ARM64 Runner

The Livepatch system allows you to apply patches to the ARM64 interpreter code at runtime without restarting the program.

## Features

- ✅ Apply patches at runtime
- ✅ Revert patches (individual or all)
- ✅ Create NOP patches for debugging
- ✅ Create branch patches for control flow redirection
- ✅ Save and load patches from files
- ✅ Thread safety (using mutexes)
- ✅ Patch statistics and monitoring
- ✅ Address and instruction validation

## File Structure

```
project_root/
├── src/                    - Source code
│   ├── arm64_runner.c  - Main ARM64 interpreter
│   └── livepatch.c         - Livepatch system
├── include/                - Header files
│   └── livepatch.h         - Livepatch header
├── examples/               - Usage examples
│   ├── livepatch_example.c         - Livepatch demo
│   ├── livepatch_security_demo.c   - Security patch example
│   ├── security_patch_example.c    - Another patch example
│   ├── test_patch.lpatch           - Patch file example
│   └── livepatch_example           - Compiled example
├── tests/                  - Tests
│   ├── add                 - Test program
│   ├── hello.s             - Test ASM file
│   └── hello               - Test binary
├── docs/                   - Documentation
│   ├── RC3_Livepatch_Persistent_Idea.md - Livepatch idea description
│   └── README.md           - Main documentation
├── patches/                - Patch files (currently empty)
├── Makefile                - Build system
├── PROJECT_STRUCTURE.md    - Project structure description
└── README.md               - Main project description
```

## Quick Start

### Build

```bash
make
```

### Run Demo

```bash
./livepatch_example demo
```

### Run Tests

```bash
make test
```

## API

### Initialization and Cleanup

```c
// Initialize the system
LivePatchSystem* livepatch_init(void* memory, size_t mem_size, uint64_t base_addr);

// Cleanup the system
void livepatch_cleanup(LivePatchSystem* system);
```

### Main Operations

```c
// Apply a patch
int livepatch_apply(LivePatchSystem* system, uint64_t target_addr, 
                   uint32_t new_instr, const char* description);

// Revert a patch
int livepatch_revert(LivePatchSystem* system, uint64_t target_addr);

// Revert all patches
int livepatch_revert_all(LivePatchSystem* system);
```

### Specialized Patches

```c
// Create a NOP patch
int livepatch_create_nop(LivePatchSystem* system, uint64_t addr, const char* description);

// Create a branch patch
int livepatch_create_branch(LivePatchSystem* system, uint64_t from_addr, 
                           uint64_t to_addr, const char* description);
```

### File Operations

```c
// Save patches to file
int livepatch_save_to_file(LivePatchSystem* system, const char* filename);

// Load patches from file
int livepatch_load_from_file(LivePatchSystem* system, const char* filename);
```

### Information and Statistics

```c
// List all patches
void livepatch_list(LivePatchSystem* system);

// System statistics
void livepatch_stats(LivePatchSystem* system);

// Demo
void livepatch_demo(LivePatchSystem* system);
```

## Usage Examples

### Basic Patch Application

```c
#include "livepatch.h"

// Initialization
void* memory = malloc(1024 * 1024);
LivePatchSystem* system = livepatch_init(memory, 1024 * 1024, 0x400000);

// Apply a patch
livepatch_apply(system, 0x4001000, 0xD503201F, "NOP patch");

// Cleanup
livepatch_cleanup(system);
free(memory);
```

### Create a NOP Patch

```c
// Replace instruction at 0x4001000 with NOP
livepatch_create_nop(system, 0x4001000, "Disable check");
```

### Create a Branch Patch

```c
// Create a branch from 0x4001000 to 0x4002000
livepatch_create_branch(system, 0x4001000, 0x4002000, "Jump to handler");
```

### Working with Patch Files

```c
// Save current patches
livepatch_save_to_file(system, "my_patches.txt");

// Load patches from file
livepatch_load_from_file(system, "my_patches.txt");
```

## Patch File Format

The patch file uses a simple text format:

```
# Comment
# Format: address instruction description
4001000 D503201F NOP patch for debugging
4002000 14000001 Jump to error handler
4003000 12345678 Custom instruction
```

## Integration with ARM64 Runner

To integrate with your ARM64 interpreter:

1. Include the header file:
```c
#include "livepatch.h"
```

2. Initialize the system after creating the state:
```c
// In init_arm64_state or main
LivePatchSystem* livepatch_system = livepatch_init(state->memory, 
                                                  state->mem_size, 
                                                  state->base_addr);
livepatch_set_system(livepatch_system);
```

3. Cleanup before exit:
```c
// In main before return
livepatch_cleanup(livepatch_get_system());
```

## Makefile Commands

```bash
make              # Build all targets
make clean        # Clean object files
make test         # Run tests
make demo         # Run demo
make create-patches # Create a patch file
make load-patches # Load patches from file
make memory-demo  # Memory demo
make help         # Show help
```

## Requirements

- GCC or compatible C compiler
- POSIX-compliant system (Linux, macOS, BSD)
- pthread library

## Security

- All memory operations are protected by mutexes
- Address validation before patching
- Memory bounds checking
- Safe resource cleanup

## Performance

- Supports up to 1000 patches simultaneously
- Fast patch application and revert
- Minimal synchronization overhead

## Debugging

To enable debug information, add to your code:

```c
// Enable tracing
#define LIVEPATCH_DEBUG 1

// Or use existing functions
livepatch_list(system);
livepatch_stats(system);
```

## License

This code is distributed under the MIT license.

## Author

Livepatch system created for the ARM64 Runner interpreter.

## Support

If you encounter problems:
1. Run tests: `make test`
2. Check the documentation
3. Create an issue with a description of the problem 