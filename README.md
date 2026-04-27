# Sol - C++ MQTT Broker

**Sol** is a lightweight, non-blocking MQTT v3.1.1 broker built from scratch in C++. It features a high-performance `kqueue`-based event loop, a custom Trie for advanced topic routing (including wildcard support), and a robust asynchronous state machine capable of handling TCP packet fragmentation.

## Architecture & How It Works

The broker is divided into several modular components that work together asynchronously:

### 1. The Event Loop (`network.cpp` / `network.hpp`)
At the core of the broker is a custom, non-blocking event loop built using macOS `kqueue`. It multiplexes all I/O events without using multiple threads.
- **Connections:** When a new client connects, the kernel notifies the event loop, triggering `on_accept`.
- **I/O:** When data arrives on a socket, the loop triggers `on_read`. When the kernel socket buffer is ready to accept outgoing data, it triggers `on_write`.
- **Timers:** The loop supports periodic tasks using `EVFILT_TIMER`, such as publishing internal broker statistics.

### 2. Client State Machine (`server.hpp` / `server.cpp`)
Because TCP is a continuous stream protocol, a single MQTT packet might arrive in multiple pieces, causing non-blocking reads to return `EAGAIN`. To handle this without blocking the server or dropping the client, each client tracks its reading phase via a state machine:
- `WAITING_HEADER`: Waiting for the MQTT Control Packet Type byte.
- `WAITING_LENGTH`: Progressively decoding the "remaining length" of the packet.
- `WAITING_DATA`: Accumulating exactly the number of payload bytes defined by the length.
- `SENDING_DATA`: The packet is fully formed and ready to be processed by a command handler.

### 3. Protocol Parsing (`mqtt.cpp` / `mqtt.hpp` / `pack.cpp`)
Once a full packet is buffered by the state machine, it is passed to the parser. 
- The parser deserializes the raw byte stream into structured C++ unions (`union mqtt_packet`). 
- Outgoing packets are serialized back into byte streams using `pack_mqtt_packet` before being sent back to the client.

### 4. Topic Routing & Wildcards (`trie.cpp` / `trie.hpp`)
MQTT allows clients to subscribe to hierarchical topics (like `home/livingroom/temp`) and use wildcards (`+` for a single level, `#` for all remaining levels). 
- **The Trie:** To route messages efficiently, the broker stores subscriptions in a Trie (Prefix Tree).
- **Matching:** When a `PUBLISH` message is received, the broker recursively walks the Trie to find all matching topic nodes, automatically honoring wildcards. It then broadcasts the message to all `std::list<subscriber>` attached to those nodes.

---------

## Guide: How to Build and Use

### Prerequisites
- macOS (requires `kqueue` and `CoreServices` for UUID generation)
- `cmake` and a modern C++ compiler (`clang++` or `g++`)
- `mosquitto` client tools (optional, but highly recommended for testing)

### Building the Broker

Navigate to the root of the project directory and run the standard CMake build process:

```bash
mkdir build
cd build
cmake ..
make -j4
```

This will compile the `sol` executable inside the `build` directory.

### Running the Broker

Start the broker by running the executable. You can provide optional flags to change the binding address, port, or enable verbose logging.

```bash
# Start the broker with default settings (127.0.0.1:1883)
./sol

# Start the broker on all interfaces, port 1883, with verbose debug logging enabled
./sol -a 0.0.0.0 -p 1883 -v
```

### Testing with Mosquitto Clients

You can test the broker using the standard Mosquitto command-line tools in separate terminal windows.

**1. Subscribe to a topic with a wildcard:**
```bash
# Subscribe to all sensor topics
mosquitto_sub -h 127.0.0.1 -p 1883 -t "home/sensors/#" -d
```

**2. Publish to a specific topic:**
```bash
# Publish a message to the temperature sensor topic
mosquitto_pub -h 127.0.0.1 -p 1883 -t "home/sensors/temperature" -m "22.5 C" -d
```

You should instantly see the `mosquitto_sub` terminal receive the `"22.5 C"` message, while the broker's verbose logs (`-v`) will display the internal packet flow (`CONNECT`, `SUBSCRIBE`, `PUBLISH`, `PUBACK`).


Client connects
   ↓
Event loop detects
   ↓
State machine rebuilds packet
   ↓
Parser understands packet
   ↓
Handler decides action
   ↓
Trie finds subscribers
   ↓
Messages queued
   ↓
Event loop sends data