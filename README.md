*** MQTT broker : each consumer client can subscribe to topics in order to receive all messages published by other clients to those topics**

# Fixed Header

 | Bit    | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 |--------|---------------|---------------|
 | Byte 1 | MQTT type     |  Flags        |
 |--------|-------------------------------|
 | Byte 2 |                               |
 |  .     |      Remaining Length         |
 |  .     |                               |
 | Byte 5 |                               |

The Fixed Header tells the broker:
    What kind of packet is this and how big is it?

bits 7 - 4

| Binary | Decimal | Packet      |
| ------ | ------- | ----------- |
| 0001   | 1       | CONNECT     |
| 0010   | 2       | CONNACK     |
| 0011   | 3       | PUBLISH     |
| 1000   | 8       | SUBSCRIBE   |
| 1010   | 10      | UNSUBSCRIBE |
| 1100   | 12      | PINGREQ     |
| 1110   | 14      | DISCONNECT  |


bits 3 - 0
| Flag   | Meaning           |
| ------ | ----------------- |
| DUP    | duplicate message |
| QoS    | quality level     |
| RETAIN | store message     |
