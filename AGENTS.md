# AGENTS.md - Codebase Guidelines

## Build Commands

```bash
# Build release version
make

# Build with basic debug flags
make debug

# Build with extensive debug flags
make debug2

# Clean and rebuild
make clean && make
```

**Compiler**: gcc with `-pthread -Wall -pedantic-errors` flags

## Testing

This project does not have automated tests. To test functionality:

1. Build with debug flags: `make debug` or `make debug2`
2. Run with sudo (required for I2C hardware access): `sudo ./dgtpicom`
3. Test modes:
   - Display text: `sudo ./dgtpicom "message" [beep] [ldots] [rdots]`
   - Run clock: `sudo ./dgtpicom [L/l/R/r] [lh] [lm] [ls] [rh] [rm] [rs]`
   - Interactive mode: `sudo ./dgtpicom` (press buttons to interact)
4. Use `~` for spinning text demo
5. Use `*` for animation demo

Debug output includes: send failures, ack failures, receive errors, max buffer usage.

## Code Style Guidelines

### General
- C99 standard with POSIX extensions
- GNU General Public License v3
- Use `char` for bytes, `int` for error codes
- Global variables for hardware registers (C convention for embedded)
- No external dependencies beyond stdlib and pthread

### Naming Conventions
- Functions: `dgtpicom_*`, `dgt3000_*`, `i2c_*` prefixes
- Types: camelCase (`dgtReceive_t`, `debug_t`)
- Constants: SCREAMING_SNAKE_CASE (`ERROR_OK`, `DGTRX_BUTTON_BUFFER_SIZE`)
- Global variables: `dgtRx`, `bug` (debug struct)

### Error Handling
- Return codes from -10 to 0 (0 = success)
- Retry logic: most functions retry up to 3 times
- Critical errors stop execution and return immediately
- Debug mode prints detailed error messages with timestamps

### Formatting
- 4-space indentation (actual indentation in codebase uses tabs)
- K&R style brace placement
- No spaces after function names
- Spaces around binary operators
- No spaces before commas/semicolons

### Comments
- Block comments for header documentation (copyright/license)
- Inline comments sparingly, only for complex logic
- Debug-specific code wrapped in `#ifdef debug` blocks

### Header Files
- Include guards: `#ifndef NAME_H #define NAME_H #endif`
- Declare functions in `.h`, implement in `.c`
- extern declarations for global variables in debug mode

### Key Files
- `dgtpicom.c` / `dgtpicom.h`: Main API
- `rpi.c` / `rpi.h`: Raspberry Pi I2C hardware access
- `clock_proto.h`: Protocol constants and structs
- `dgtpicom_dgt3000.h`: DGT3000-specific definitions
- `main.c`: Command-line application

### Important Notes
- Requires root access (`sudo`) for `/dev/mem` access
- Core frequency must be locked (add `core_freq=250` to `/boot/config.txt`)
- Uses hardware I2C on GPIO pins (2/3 or 10/11 on Pi 4)
- Real-time thread with FIFO scheduling for receiving messages