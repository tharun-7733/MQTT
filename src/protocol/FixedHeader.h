#pragma once
#include "mqttTypes.h"

namespace mqtt{
    struct FixedHeader
    {
        uint8_t raw{0};


        PacketTypes type() const{
            return static_cast<PacketTypes>((raw >> 4) & 0x0F);
        }
        bool dup() const{
            return ((raw >> 3) & 0x01);
        }
        
        QoS qos() const{
            return static_cast<QoS> ((raw >> 1) & 0x03);
        }

        bool retain() const{
            return raw & 0x01;
        }
        static uint8_t build(PacketTypes type,
                                bool dup = false, 
                                QoS qos = QoS::AT_MOST_ONCE,
                                bool retain = false   
                            )
        {
            uint8_t byte = 0;

            byte |= static_cast<uint8_t> (type) << 4;
            byte |= (dup ? 1 : 0) << 3;
            byte |= static_cast<uint8_t> (qos) << 1;
            byte |= (retain ? 1 : 0);
            return byte;
        }

    };
    
}