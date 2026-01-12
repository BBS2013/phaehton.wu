#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "doip_common.h"

const uint16_t TESTER_LOGICAL_ADDR = 0x0E80;

// 辅助函数：接收指定长度的数据
bool recv_fixed(int sock, void* buffer, size_t length) {
    size_t total_read = 0;
    char* ptr = (char*)buffer;
    while (total_read < length) {
        ssize_t n = recv(sock, ptr + total_read, length - total_read, 0);
        if (n <= 0) return false;
        total_read += n;
    }
    return true;
}

int main() {
    std::cout << "=== DoIP 诊断仪模拟器 (C++ Version) ===" << std::endl;

    // 1. 车辆发现 (UDP)
    std::cout << "\n[Step 1] 发送车辆发现请求 (UDP Broadcast)..." << std::endl;
    
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DOIP_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    DoIPHeader req_header;
    fill_header(&req_header, PAYLOAD_TYPE_VEHICLE_IDENT_REQ, 0);
    
    sendto(udp_sock, &req_header, sizeof(req_header), 0, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    std::string target_ip;
    uint16_t vehicle_logical_addr = 0;

    char buffer[1024];
    sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t n = recvfrom(udp_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (n >= (ssize_t)sizeof(DoIPHeader)) {
        DoIPHeader* header = (DoIPHeader*)buffer;
        parse_header_ntoh(header);

        if (header->payload_type == PAYLOAD_TYPE_VEHICLE_IDENT_RES) {
            target_ip = inet_ntoa(from_addr.sin_addr);
            std::cout << "[UDP] 收到响应 来自 " << target_ip << std::endl;
            
            // Payload 偏移
            uint8_t* payload = (uint8_t*)(buffer + sizeof(DoIPHeader));
            
            // VIN 17 bytes
            std::string vin((char*)payload, 17);
            std::cout << "       -> 发现车辆 VIN: " << vin << std::endl;

            // Logical Addr
            vehicle_logical_addr = (payload[17] << 8) | payload[18];
            std::cout << "       -> 逻辑地址: 0x" << std::hex << vehicle_logical_addr << std::dec << std::endl;
        }
    } else {
        std::cout << "[!] 未发现车辆，尝试直连 127.0.0.1" << std::endl;
        target_ip = "127.0.0.1";
        vehicle_logical_addr = 0x1000;
    }
    close(udp_sock);

    if (target_ip.empty()) return 1;

    // 2. 建立 TCP 连接
    std::cout << "\n[Step 2] 连接车辆 TCP " << target_ip << ":" << DOIP_PORT << "..." << std::endl;
    int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DOIP_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(target_ip.c_str());

    if (connect(tcp_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[!] 连接失败");
        return 1;
    }

    // 3. 路由激活
    std::cout << "\n[Step 3] 发送路由激活请求..." << std::endl;
    // Source Address (2) + Activation Type (1) + Reserved (4)
    std::vector<uint8_t> ra_payload(7);
    ra_payload[0] = (TESTER_LOGICAL_ADDR >> 8) & 0xFF;
    ra_payload[1] = TESTER_LOGICAL_ADDR & 0xFF;
    ra_payload[2] = 0x00; // Type
    // Reserved 0...

    DoIPHeader ra_header;
    fill_header(&ra_header, PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ, ra_payload.size());

    send(tcp_sock, &ra_header, sizeof(ra_header), 0);
    send(tcp_sock, ra_payload.data(), ra_payload.size(), 0);

    // 接收激活响应
    DoIPHeader res_header;
    if (recv_fixed(tcp_sock, &res_header, sizeof(res_header))) {
        parse_header_ntoh(&res_header);
        std::vector<uint8_t> res_payload(res_header.payload_length);
        if (recv_fixed(tcp_sock, res_payload.data(), res_header.payload_length)) {
            if (res_header.payload_type == PAYLOAD_TYPE_ROUTING_ACTIVATION_RES) {
                // ... Source ... Target ... Code (index 4)
                uint8_t code = res_payload[4];
                if (code == 0x10) {
                    std::cout << "[TCP] 路由激活成功! (Code 0x10)" << std::endl;
                } else {
                    std::cout << "[!] 路由激活失败: 0x" << std::hex << (int)code << std::dec << std::endl;
                    close(tcp_sock);
                    return 1;
                }
            }
        }
    }

    // 4. 发送诊断请求
    std::cout << "\n[Step 4] 发送诊断请求 (ReadDataByIdentifier 0x22 F1 90)..." << std::endl;
    // Payload: Source (2) + Target (2) + UDS Data
    std::vector<uint8_t> diag_payload;
    diag_payload.push_back((TESTER_LOGICAL_ADDR >> 8) & 0xFF);
    diag_payload.push_back(TESTER_LOGICAL_ADDR & 0xFF);
    diag_payload.push_back((vehicle_logical_addr >> 8) & 0xFF);
    diag_payload.push_back(vehicle_logical_addr & 0xFF);
    // UDS: 22 F1 90
    diag_payload.push_back(0x22);
    diag_payload.push_back(0xF1);
    diag_payload.push_back(0x90);

    DoIPHeader diag_header_req;
    fill_header(&diag_header_req, PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE, diag_payload.size());
    
    send(tcp_sock, &diag_header_req, sizeof(diag_header_req), 0);
    send(tcp_sock, diag_payload.data(), diag_payload.size(), 0);

    // 5. 接收响应
    std::cout << "\n[Step 5] 等待响应..." << std::endl;
    while (true) {
        DoIPHeader h;
        if (!recv_fixed(tcp_sock, &h, sizeof(h))) break;
        parse_header_ntoh(&h);

        std::vector<uint8_t> p(h.payload_length);
        if (h.payload_length > 0) {
            if (!recv_fixed(tcp_sock, p.data(), h.payload_length)) break;
        }

        if (h.payload_type == PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK) {
            std::cout << "[TCP] 收到 DoIP ACK (Positive)" << std::endl;
        } else if (h.payload_type == PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE) {
            uint16_t sa = (p[0] << 8) | p[1];
            uint16_t ta = (p[2] << 8) | p[3];
            std::cout << "[TCP] 收到诊断响应 SA=0x" << std::hex << sa << " TA=0x" << ta << std::dec << std::endl;
            
            std::cout << "       -> UDS Data: ";
            for (size_t i = 4; i < p.size(); i++) {
                printf("%02X ", p[i]);
            }
            std::cout << std::endl;
            break; // 演示结束
        }
    }

    std::cout << "\n=== 演示结束 ===" << std::endl;
    close(tcp_sock);
    return 0;
}
