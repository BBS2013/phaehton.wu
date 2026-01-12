#ifndef DOIP_COMMON_H
#define DOIP_COMMON_H

#include <cstdint>
#include <arpa/inet.h> // for htons, htonl

// DoIP 常量定义
const uint8_t PROTOCOL_VERSION = 0x02;
const uint8_t INVERSE_PROTOCOL_VERSION = 0xFD;

const uint16_t PAYLOAD_TYPE_VEHICLE_IDENT_REQ = 0x0001;
const uint16_t PAYLOAD_TYPE_VEHICLE_IDENT_RES = 0x0004;
const uint16_t PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ = 0x0005;
const uint16_t PAYLOAD_TYPE_ROUTING_ACTIVATION_RES = 0x0006;
const uint16_t PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE = 0x8001;
const uint16_t PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK = 0x8002;
const uint16_t PAYLOAD_TYPE_DIAGNOSTIC_NEG_ACK = 0x8003;

const int DOIP_PORT = 13400;

// 使用 packed 属性确保结构体没有填充字节，直接对应网络字节流
struct DoIPHeader {
    uint8_t protocol_version;
    uint8_t inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
} __attribute__((packed));

// 辅助函数：创建头部 (处理字节序)
inline void fill_header(DoIPHeader* header, uint16_t type, uint32_t length) {
    header->protocol_version = PROTOCOL_VERSION;
    header->inverse_protocol_version = INVERSE_PROTOCOL_VERSION;
    header->payload_type = htons(type);
    header->payload_length = htonl(length);
}

// 辅助函数：解析头部 (处理字节序)
inline void parse_header_ntoh(DoIPHeader* header) {
    header->payload_type = ntohs(header->payload_type);
    header->payload_length = ntohl(header->payload_length);
}

#endif // DOIP_COMMON_H
