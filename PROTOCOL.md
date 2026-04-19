# DGT3000 I2C Protocol Specification

## Overview

The DGT3000 chess clock uses a proprietary I2C-like protocol over GPIO pins. While it uses SDA/SCL lines similar to standard I2C, it has significant differences that prevent it from being compatible with standard I2C controllers. This document describes the complete low-level protocol.

## Hardware Interface

### Pinout (Raspberry Pi)

| Function | Pi B/B+/Zero | Pi 2/3 | Pi 4 |
|----------|-------------|--------|------|
| SCL (Clock) | GPIO2 (pin 3) | GPIO2 (pin 3) | GPIO10 (pin 19) |
| SDA (Data) | GPIO3 (pin 5) | GPIO3 (pin 5) | GPIO11 (pin 23) |

### GPIO Configuration

- **Standard mode**: GPIO2/3 as I2C (ALT0)
- **Pi 4 mode**: GPIO10/11 as I2C (ALT3)
- **Reset mode**: Pins configured as input to reset I2C master

### Timing Requirements

- **Core frequency**: Must be locked to 250 MHz (add `core_freq=250` to `/boot/config.txt`)
- **I2C clock speed**: ~95 kHz (divider set dynamically based on core frequency)
- **Bus free time**: Minimum 10ms between transfers
- **Slave response time**: ~50μs after bus becomes free

## Protocol Frame Format

Each message follows this structure:

```
Byte 0: 0x50 (fixed start byte for master → slave)
Byte 1: Address (always 0x10 for DGT clock)
Byte 2: Length (total packet length - 1)
Byte 3: Command type (message ID)
Byte 4+: Data payload
Last: CRC-8 checksum
```

### Message Structure Details

```
[0x50] [ADDR] [LEN] [CMD] [DATA...] [CRC]
```

- **Address**: Always `0x10` (16 decimal) - the DGT clock's I2C address
- **Length**: Total packet length minus 1 (includes bytes 2 through end)
- **Command**: Identifies message type (1-16)
- **CRC**: CRC-8 checksum using ATM-8 polynomial (x^8 + x^2 + x^1 + x^1)

### CRC-8 Algorithm

The protocol uses CRC-8 with the following parameters:
- Polynomial: 0x07 (x^8 + x^2 + x^1 + x^0)
- Initial value: 0x00
- Input reflected: Yes
- Output reflected: Yes

Pre-calculated lookup table (256 entries) is used for efficiency.

## Command Types

| ID | Name | Direction | Length | Description |
|----|------|-----------|--------|-------------|
| 1 | Ack | Slave → Master | 6 | Acknowledgment response |
| 2 | Hello | Slave → Master | 4 | Device initialization |
| 3 | Debug | Slave → Master | Variable | Debug messages |
| 4 | Time | Slave → Master | 21 | Clock time broadcast |
| 5 | Button | Slave → Master | Variable | Button press events |
| 6 | Display | Master → Slave | Variable | Set display text |
| 7 | End Display | Master → Slave | 5 | Clear display |
| 8 | Current Program | Master → Slave | 6 | Query current timer |
| 9 | Program | Master → Slave | Variable | Set timer values |
| 10 | Set and Run | Master → Slave | 12 | Start/stop timers |
| 11 | Change State | Master → Slave | 6 | Enter/exit CC mode |
| 12 | Send Hello | Master → Slave | 3 | Wake up device |
| 13 | Ping | Master → Slave | 5 | Keep-alive |
| 14 | Time Correlation | Master → Slave | 8 | Time sync |
| 15 | Set Central Control | Master → Slave | 5 | Enable remote control |
| 16 | Release Central Control | Master → Slave | 5 | Disable remote control |
| 17 | Trigger Boot Loader | Master → Slave | 3 | Enter bootloader |

## Message Descriptions

### 1. Acknowledgment (Ack)

**Direction**: Slave → Master  
**Length**: 6 bytes  
**Structure**:
```
[0x50] [0x10] [0x05] [0x01] [ACK_ID] [FLAGS] [CRC]
```

- **ACK_ID**: Command ID being acknowledged (1-17)
- **FLAGS**: Bit 3 = positive ack, bit 2 = negative ack

### 2. Hello

**Direction**: Slave → Master  
**Length**: 4 bytes  
**Structure**:
```
[0x50] [0x10] [0x03] [0x02] [CRC]
```

Sent by clock when:
- Bus becomes free (master starts listening)
- After receiving a wake/ping command

### 3. Time Broadcast

**Direction**: Slave → Master  
**Length**: 21 bytes  
**Structure**:
```
[0x50] [0x10] [0x14] [0x04]
[LEFT_HOURS] [LEFT_MINUTES_SEC] [LEFT_SECONDS]
[0x00] [0x00] [0x00]
[RIGHT_HOURS] [RIGHT_MINUTES_SEC] [RIGHT_SECONDS]
[0x00] [0x00] [0x00] [0x00]
[FLAG]
[CRC]
```

**Time Format** (BCD encoding):
- Hours: Low 4 bits only (0-9)
- Minutes: High 4 bits = tens, low 4 bits = ones (00-59)
- Seconds: High 4 bits = tens, low 4 bits = ones (00-59)

**Example**: Minutes = 0x35 = 35 minutes

**Flags**:
- Bit 0: Left timer running (1) / stopped (0)
- Bit 1: Right timer running (1) / stopped (0)
- Bit 0: Lever position (0 = left down, 1 = right down)

### 4. Button Events

**Direction**: Slave → Master  
**Length**: Variable (4-5 bytes)  
**Structure**:
```
[0x50] [0x10] [0x03-0x04] [0x05]
[PREV_STATE] [CURR_STATE] [0x00] [CRC]
```

**Button States** (new state in byte 4):
```
0x01 - Back button (<)
0x02 - Minus button (-)
0x04 - Play/Pause button (p)
0x08 - Plus button (+)
0x10 - Forward button (>)
0x20 - Power button (on/off)
0x40 - Lever moved (right side down)
0x80 - Lever moved (left side down)
```

**Special Messages**:
- Power on/off: State changes from 0x20 to 0xA0 (bit 7 indicates power state)
- Lever change: 0x40 or 0x80 indicates which side is down

### 5. Display Command

**Direction**: Master → Slave  
**Length**: 21 bytes  
**Structure**:
```
[0x50] [0x10] [0x14] [0x06]
[11 chars of text]
[0xFF] [CRC padding]
[0x00] [CRC]  // beep parameter
[0x03] [CRC]  // left dots
[0x00] [CRC]  // right dots
[CRC]
```

**Text Format**: 11 ASCII characters, space-padded (0x20)  
**Beep**: Duration in 62.5ms units (max 48 = 3s)  
**Dots/Icons**: Bitmask:
```
Left: 0x01 (flag), 0x02 (white king), 0x04 (black king), 
      0x08 (colon), 0x10 (dot), 0x20 (extra dot)
Right: 0x01 (flag), 0x02 (white king), 0x04 (black king), 
       0x08 (colon), 0x10 (dot)
```

### 6. End Display

**Direction**: Master → Slave  
**Length**: 5 bytes  
**Structure**:
```
[0x50] [0x10] [0x04] [0x07] [CRC]
```

Clears the display and returns to clock mode.

### 7. Set and Run (SetnRun)

**Direction**: Master → Slave  
**Length**: 12 bytes  
**Structure**:
```
[0x50] [0x10] [0x0B] [0x0A]
[LEFT_HOURS] [LEFT_MIN] [LEFT_SEC]
[RIGHT_HOURS] [RIGHT_MIN] [RIGHT_SEC]
[MODES] [CRC]
```

**Modes byte**:
- Bits 0-1: Left run mode (0=stop, 1=count down, 2=count up)
- Bits 2-3: Right run mode (0=stop, 1=count down, 2=count up)

### 8. Change State

**Direction**: Master → Slave  
**Length**: 6 bytes  
**Structure**:
```
[0x50] [0x10] [0x05] [0x0B] [MODE] [CRC]
```

**Mode**:
- `0x39` (57): Enter central control (CC) mode
- `0x00` (0): Exit CC mode (power off)

### 9. Set Central Control

**Direction**: Master → Slave  
**Length**: 5 bytes  
**Structure**:
```
[0x50] [0x10] [0x04] [0x0F] [CRC]
```

Enables remote control of the clock.

### 10. Wake (Send Hello)

**Direction**: Master → Slave  
**Length**: 3 bytes  
**Structure**:
```
[0x50] [0x10] [0x02] [0x0D] [CRC]
```

Sends ping command to wake up the clock.

### 11. Ping

**Direction**: Master → Slave  
**Length**: 5 bytes  
**Structure**:
```
[0x50] [0x10] [0x04] [0x0D] [CRC]
```

Keep-alive command.

## I2C Communication Flow

### Master Sending a Message

1. **Wait for bus free**: Check SCL and SDA are high, slave FIFO empty
2. **Set destination**: Write I2C master address register
3. **Configure length**: Write message length to DLEN register
4. **Fill FIFO**: Write message bytes (excluding start byte)
5. **Start transmission**: Write control register
6. **Wait for completion**: Poll status register
7. **Error handling**:
   - `0x100`: Byte not acknowledged (NACK) - device off or collision
   - `0x200`: Clock stretch timeout (CST) - collision
   - Bus stuck: SDA/SCL low after transfer

### Slave Receiving a Message

1. **Listen for broadcasts**: Slave configured to address 0x00
2. **Hardware reception**: I2C hardware receives full packet
3. **Interrupt handling**: Thread polls receive status
4. **Buffer management**: Hardware FIFO → software buffer
5. **Validation**:
   - Address check (must be 0x10)
   - CRC validation
   - Size validation
6. **Error codes**:
   - `-6`: Timeout (10ms receive timeout)
   - `-7`: CRC error
   - `-8`: Hardware buffer overrun
   - `-9`: Software buffer overrun
   - `-2`: Wrong address
   - `-5`: Lines low (collision)
   - `-4`: Clock stretch timeout

## State Machine

### Clock Power States

```
[OFF] --> [Wake/Ping] --> [ON] --> [Set Central Control] --> [CC Mode]
                                    |                        |
                                    |--> [Change State 0x39]--+
                                    |
                                    +--> [Display/Commands] --> [ON]
```

### Connection States

1. **Initial**: Clock off, bus may be busy
2. **Wake sequence**: Ping → wait for Hello (5-10ms)
3. **Setup**: Set CC → Change State (mode 25)
4. **Operation**: Standard command/response
5. **Recovery**: On errors, reset I2C and retry

## Error Recovery

### Common Errors

| Error Code | Name | Cause | Recovery |
|------------|------|-------|----------|
| -10 | ERROR_MEM | Not running as root | Run with sudo |
| -6 | ERROR_TIMEOUT | Bus free timeout | Wait and retry |
| -7 | ERROR_CRC | CRC mismatch | Retry message |
| -8 | ERROR_HWB_FULL | Hardware buffer overrun | Reduce message rate |
| -9 | ERROR_SWB_FULL | Software buffer overrun | Read messages faster |
| -2 | ERROR_NOACK | No acknowledgment | Check device power |
| -1 | ERROR_NACK | Negative ACK | Check device state |
| -5 | ERROR_LINES | Lines low | Check wiring |
| -4 | ERROR_CST | Clock stretch timeout | Retry message |

### Recovery Procedures

1. **I2C Reset**: Reconfigure GPIO pins, reset master/slave registers
2. **Wake Recovery**: Send ping, wait for hello, retry setup
3. **CC Recovery**: Release CC, re-enter CC mode
4. **Complete Reset**: Power cycle clock, restart initialization

## Message Timing

### Inter-Message Delays

- **Minimum bus free time**: 10ms
- **Slave response time**: 50μs after bus free
- **Master retry delay**: 10ms
- **Button repeat delay**: 800ms initial, 400ms subsequent

### Timeout Values

| Operation | Timeout |
|-----------|---------|
| Bus free wait | 10ms |
| Buffer space wait | 10ms |
| Message completion | 10ms |
| ACK receive | 10ms |
| Hello after ping | 10ms |
| Display clear (fast) | 1.2ms |
| Display clear (slow) | 10ms |

## GPIO Register Map

### I2C Master Registers (offsets from base)

| Offset | Register | Description |
|--------|----------|-------------|
| +0x00 | I2CS | Control/status |
| +0x04 | DLEN | Data length |
| +0x08 | A | Slave address |
| +0x0C | FIFO | Data FIFO |
| +0x10 | DIV | Clock divisor |
| +0x14 | DEL | Delay register |

### I2C Slave Registers (offsets from base)

| Offset | Register | Description |
|--------|----------|-------------|
| +0x00 | SLV | Slave address |
| +0x04 | CR | Control register |
| +0x08 | FR | FIFO and status |
| +0x0C | RSR | Receive status |

### GPIO Registers

| Register | Description |
|----------|-------------|
| GPSET0 | GPIO output set |
| GPCLR0 | GPIO output clear |
| GPLEV0 | GPIO level read |

## Initialization Sequence

```c
1. Open /dev/mem (requires root)
2. mmap GPIO, timer, I2C registers
3. Configure GPIO2/3 or GPIO10/11 as I2C (ALT0/ALT3)
4. Set I2C clock divider: 1000*freq/95
5. Configure I2C slave: address 0x00, enable
6. Reset I2C master/slave hardware
7. Start receive thread (FIFO priority)
8. Send ping to wake device
9. Wait for hello message
10. Send Set Central Control
11. Send Change State (mode 25)
```

## Notes

- This is NOT standard I2C: Uses different addressing, timing, and protocol
- No start/stop conditions: Continuous data stream
- No clock stretching: Slave must respond quickly
- No multi-master support: Only one master (Raspberry Pi)
- CRC is computed over entire packet (including length byte)
- Device always responds from address 0x10 regardless of address sent
- Message IDs start from 1 (not 0)
- Time values are partially BCD-encoded
- Lever position is in time messages (bit 0 of byte 19)