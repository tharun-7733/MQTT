# MQTT Broker

> A lightweight **MQTT broker implementation in C++** built from scratch to understand the internal working of the MQTT protocol — packet parsing, serialization, and broker message routing.

---

## 📡 What is MQTT?

**MQTT (Message Queuing Telemetry Transport)** is a lightweight publish–subscribe messaging protocol widely used in:

* IoT devices
* Smart homes
* Sensors & telemetry systems
* Embedded systems
* Low-bandwidth networks

Instead of direct communication:

```
Client → Broker → Subscribers
```

Clients publish messages to **topics**, and subscribed clients receive them.

---

## 🏗️ Project Structure

```
MQTT/
│
├── sol/
│   ├── src/protocol/
│   │   ├── FixedHeader.h
│   │   ├── packetParser.cpp
│   │   ├── packetSerializer.cpp
│   │   ├── packets.h
│   │   └── remainingLength.*
│   │
│   └── build/        (ignored)
│
├── CMakeLists.txt
└── README.md
```

---

## ⚙️ Build Instructions

```bash
mkdir build
cd build
cmake ..
make
./mqtt_broker
```

---

# 📦 MQTT Packet Structure

Every MQTT packet contains:

```
+------------------+
| Fixed Header     |
+------------------+
| Variable Header  |
+------------------+
| Payload          |
+------------------+
```

---

# 🔹 Fixed Header

The Fixed Header tells the broker:

✅ What kind of packet is this?
✅ How large is the message?

---

<details>
<summary>🔎 Bit Layout (Click to Expand)</summary>

```
Bit:   7   6   5   4 | 3 | 2 | 1 | 0
       -------------------------------
       Packet Type    |     Flags
```

```
Byte 1  → MQTT Control Type + Flags
Byte 2+ → Remaining Length
```

</details>

---

## Fixed Header Structure

| Byte | Bits     | Field               | Description              |
| ---- | -------- | ------------------- | ------------------------ |
| 1    | 7–4      | Control Packet Type | MQTT message type        |
| 1    | 3–0      | Flags               | Packet-specific flags    |
| 2–5  | Variable | Remaining Length    | Size of remaining packet |

---

<details>
<summary>📡 Control Packet Types</summary>

| Binary | Decimal | Packet      |
| ------ | ------- | ----------- |
| 0001   | 1       | CONNECT     |
| 0010   | 2       | CONNACK     |
| 0011   | 3       | PUBLISH     |
| 0100   | 4       | PUBACK      |
| 1000   | 8       | SUBSCRIBE   |
| 1001   | 9       | SUBACK      |
| 1010   | 10      | UNSUBSCRIBE |
| 1100   | 12      | PINGREQ     |
| 1101   | 13      | PINGRESP    |
| 1110   | 14      | DISCONNECT  |

</details>

---

<details>
<summary>⚙️ Flags (PUBLISH Packet)</summary>

| Bit | Name   | Meaning            |
| --- | ------ | ------------------ |
| 3   | DUP    | Duplicate delivery |
| 2–1 | QoS    | Quality of Service |
| 0   | RETAIN | Retain message     |

### QoS Levels

| Bits | QoS           |
| ---- | ------------- |
| 00   | At most once  |
| 01   | At least once |
| 10   | Exactly once  |

</details>

---

<details>
<summary>📏 Remaining Length Encoding</summary>

Remaining Length uses **Variable Byte Integer encoding**.

Each byte:

```
Bit 7 → Continuation bit
Bits 6–0 → Value (0–127)
```

Algorithm:

```cpp
int multiplier = 1;
int value = 0;

do {
    byte = readByte();
    value += (byte & 127) * multiplier;
    multiplier *= 128;
} while (byte & 128);
```

✔ MSB = 1 → more bytes follow
✔ MSB = 0 → last byte

</details>

---

## 🧠 Mental Model

```
Byte 1 → WHAT packet?
Byte 2+ → HOW BIG is it?
```

---

## 🔬 Example

```
Hex: 30 0A
```

| Byte | Meaning                     |
| ---- | --------------------------- |
| 0x30 | PUBLISH packet              |
| 0x0A | Remaining length = 10 bytes |

---

# 🚀 Features Implemented

* MQTT Fixed Header parsing
* Packet serialization
* Remaining Length decoding
* CONNECT / PUBLISH / SUBSCRIBE handling
* Modular packet architecture

---

# 🎯 Learning Goals

This project focuses on understanding:

* Network protocol design
* Binary parsing
* Bit manipulation
* Broker architecture
* C++ systems programming

---

# 🛠️ Tech Stack

* **C++17**
* **CMake**
* TCP Socket Programming
* MQTT Protocol (v3.1.1 concepts)

---

# 📚 Future Improvements

* QoS message flow
* Retained messages
* Persistent sessions
* Multi-client handling
* Async networking

---

# 👨‍💻 Author

**Tharun Tej**

GitHub: https://github.com/tharun-7733

