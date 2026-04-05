# DGT3000 I2C Wire Protocol

This document describes the I2C protocol used to communicate with the DGT3000 electronic chess clock.

## I2C Addresses

| Address | Direction | Description |
|---------|-----------|-------------|
| 0x08 | Master→Clock | Master sends commands to clock |
| 0x10 | Clock→Master | Clock sends responses/button events |
| 0x00 | Broadcast | Clock broadcasts time/button updates to all listeners |

## Message Format

All messages follow this structure:

```
[Destination Address] [Command] [Length] [Data...] [CRC]
       1 byte           1 byte    1 byte   N bytes   1 byte
```

- **Destination Address**: I2C write address (0x08 for commands, 0x00 for display)
- **Command**: Packet type identifier (see Packet Types below)
- **Length**: Number of data bytes (excludes address, command, length, and CRC)
- **Data**: Payload bytes
- **CRC**: ATM-8 CRC (x⁸ + x² + x + 1)

## Packet Types

| Type | Command Byte | Description |
|------|--------------|-------------|
| 0 | 0x00 | Ack - Acknowledgment response |
| 1 | 0x01 | Hello - Clock response to wake |
| 2 | 0x02 | Debug - Debug information |
| 3 | 0x03 | Time - Time update (broadcast) |
| 4 | 0x04 | Button - Button state change (broadcast) |
| 5 | 0x06 | Display - Display message sent to clock |
| 6 | 0x07 | End Display - Clear display |
| 7 | 0x08 | Current Program - Clock program state |
| 8 | 0x09 | Program - Program data |
| 10 | 0x0A | Set And Run - Set clock time and start running |
| 11 | 0x0B | Change State - Mode change (e.g., mode 25) |
| 12 | 0x0C | Send Hello - Request hello message |
| 13 | 0x0D | Ping - Wake the clock |
| 14 | 0x0E | Time Correlation - Time sync |
| 15 | 0x0F | Set Central Control - Gain control of clock |
| 16 | 0x10 | Release Central Control - Release control |
| 17 | 0x11 | Trigger Boot Loader - Enter bootloader |

## Common Commands

### 1. Set Central Control (0x0F)

Acquires control of the clock.

```
0x08: 0x10 0x10 0x01 0x40 [CRC]
       ↑    ↑    ↑    ↑
       |    |    |    +-- Command: 0x0F = Set Central Control
       |    |    +-- Length: 1 byte
       |    +-- Command: 0x10 = Ping/Hello request
       +-- Address: 0x08 = I2C write to clock
```

**Expected response**: Ack from 0x10 with positive acknowledgment (byte 1 = 0x08)

### 2. Set Mode 25 (0x0B)

Configures the clock to mode 25 (standard chess mode).

```
0x08: 0x10 0x06 0x00 0x80 0x14 [CRC]
       ↑    ↑    ↑    ↑
       |    |    |    +-- Mode: 0x14 = Mode 25
       |    |    +-- Sub-command: 0x00
       |    +-- Length: 6 bytes
       +-- Address: 0x08
```

### 3. Display Text (0x06)

Sends text to display on the clock.

```
0x00: 0x08 0x02 0x00 [text...] [CRC]
       ↑    ↑    ↑
       |    |    +-- Length
       |    +-- Command: 0x02 = Display
       +-- Address: 0x00 (broadcast)
```

Text is sent as 7-segment encoded bytes (one byte per display position, with spaces for blank).

### 4. Set and Run (0x0A)

Sets the clock time and starts it running.

```
0x08: 0x10 0x05 0x40 [lr] [lh] [lm] [ls] [rr] [rh] [rm] [rs] [CRC]
       ↑    ↑    ↑    ↑
       |    |    |    +-- Command: 0x0A = Set And Run
       |    |    +-- Length: 12 bytes
       +-- Address: 0x08
```

Parameters:
- **lr**: Left clock run mode (0=stop, 1=count down, 2=count up)
- **lh/lm/ls**: Left hours, minutes, seconds
- **rr/rh/rm/rs**: Right hours, minutes, seconds

### 5. End Display (0x07)

Returns clock to normal operation mode.

```
0x08: 0x10 0x05 0x07 [CRC]
       ↑    ↑    ↑
       |    |    +-- Command: 0x07 = End Display
       |    +-- Length
       +-- Address: 0x08
```

## Button/Input Events (Broadcast from 0x00)

The clock broadcasts button events on address 0x00. The data format is:

```
[Command 0x04] [Button State] [Button Count] [Time 100ms...] [additional data...]
```

### Button Encoding

| Value | Button |
|-------|--------|
| 0x01 | Back (<) |
| 0x02 | Minus (-) |
| 0x04 | Play/Pause (▶) |
| 0x08 | Plus (+) |
| 0x10 | Forward (>) |
| 0x20 | On/Off |
| 0x40 | Lever changed (right side down) |
| 0xC0 | Lever changed (left side down) |

### Lever Events

When the lever position changes:
- **0xC0** (0x40 << 1): Left lever moved down (left side showing)
- **0x80** (0x40): Right lever moved down (right side showing)

From the capture, lever events on left side press:
```
0x00 0x08 0x06 0x00 0x80 0x14 0x00 0x40 0x00 0x0F...
                                      ↑    ↑
                                      |    +-- Button state: 0x40 = lever right down
                                      +-- Button change indicator
```

## CRC Calculation

The protocol uses ATM-8 CRC (polynomial x⁸ + x² + x + 1, equivalent to 0x07):

```c
const char crc_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31,
    // ... (full table in clock_proto.c)
};
```

## Message Flow Examples

### Initialization Sequence

1. **Wake** (optional): Send ping to 0x08, wait for Hello from 0x10
2. **Set Central Control**: Send to 0x08, get Ack from 0x10
3. **Set Mode 25**: Send mode 25 command, get Ack
4. **End Display**: Return to clock mode

### Regular Polling

The master polls every ~2 seconds for time/button updates:
- Sends command 0x06 (type 3) to 0x08
- Clock broadcasts time update from 0x00

### Button/Lever Detection

1. Master polls periodically
2. When lever changes, clock broadcasts button event (command 0x04) from 0x00
3. Button state byte indicates lever position:
   - Bit 6 (0x40) set = lever changed
   - Combined with 0x80 for left, 0x40 for right

## Captured Protocol Examples

### Display "Hello" text (22:50:46.984):
```
0x20 0x08 0x02 0x00 0x20 0x70 0x08 0x14 0x50
```

### Time polling response (22:50:46.986):
```
0x00 0x08 0x06 0x00 0x80 0x14 0x00 0x00 0x00 0x00 0x08 0x00 0x10 0x00 0x00 0x00 0x00 0x00 0x00 0x0C 0x81 0x40 0x00 0x11 0x10 0x25 0x2A
```

### Lever event - left side pressed (22:51:02.008):
```
0x10 0x10 0x01 0x81 0x63 0x95
```
(Note: Button state 0x81 indicates button change at address 0x01)

### Lever event polling showing button state change (22:51:02.959):
```
0x00 0x08 0x06 0x00 0x80 0x14 0x00 0x40 0x00 0x0F 0x08 0x00 0x10...
                                      ↑    ↑
                                      |    +-- Button: 0x0F = multiple buttons
                                      +-- 0x40 = lever state changed
```

## Notes

- The master listens on 0x00 for broadcasts and 0x10 for direct responses
- Most commands require acknowledgment from 0x10
- The clock continuously broadcasts time updates when in clock mode
- Button events are embedded in the time polling response
- CRC is calculated using the pre-computed table in clock_proto.c
