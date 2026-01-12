#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "doip_common.h"

// 模拟车辆信息
const std::string VIN = "DOIPDEMOCAR12345"; // 16 chars
const uint16_t LOGICAL_ADDRESS = 0x1000;

void udp_listener() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("UDP socket creation failed");
        return;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(DOIP_PORT);

    if (bind(sockfd, (const sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("UDP bind failed");
        close(sockfd);
        return;
    }

    std::cout << "[*] UDP 监听端口 " << DOIP_PORT << " (车辆发现)" << std::endl;

    while (true) {
        char buffer[1024];
        sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);

        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *)&cliaddr, &len);
        if (n < (ssize_t)sizeof(DoIPHeader)) continue;

        DoIPHeader* header = (DoIPHeader*)buffer;
        parse_header_ntoh(header);

        if (header->payload_type == PAYLOAD_TYPE_VEHICLE_IDENT_REQ) {
            std::cout << "[UDP] 收到车辆发现请求 来自 " << inet_ntoa(cliaddr.sin_addr) << std::endl;

            // 构建响应 Payload
            // VIN (17) + Logical Address (2) + EID (6) + GID (6) + Further Action (1) + Sync Status (1)
            // 总共 33 字节。这里简化填充
            std::vector<uint8_t> payload;
            
            // VIN 17 bytes (padded with null if needed)
            for (size_t i = 0; i < 17; i++) {
                if (i < VIN.length()) payload.push_back(VIN[i]);
                else payload.push_back(0);
            }

            // Logical Address 2 bytes
            payload.push_back((LOGICAL_ADDRESS >> 8) & 0xFF);
            payload.push_back(LOGICAL_ADDRESS & 0xFF);

            // EID (6) + GID (6) + FA (1) + Sync (1) = 14 bytes of zeros
            for (int i = 0; i < 14; i++) payload.push_back(0);

            // Header
            DoIPHeader res_header;
            fill_header(&res_header, PAYLOAD_TYPE_VEHICLE_IDENT_RES, payload.size());

            // 发送
            std::vector<uint8_t> packet(sizeof(DoIPHeader) + payload.size());
            memcpy(packet.data(), &res_header, sizeof(DoIPHeader));
            memcpy(packet.data() + sizeof(DoIPHeader), payload.data(), payload.size());

            sendto(sockfd, packet.data(), packet.size(), 0, (const sockaddr *)&cliaddr, len);
        }
    }
    close(sockfd);
}

void handle_tcp_client(int client_sock, sockaddr_in addr) {
    bool is_activated = false;
    std::cout << "[TCP] 新连接: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;

    while (true) {
        DoIPHeader header;
        // 读取头部
        ssize_t n = recv(client_sock, &header, sizeof(header), 0);
        if (n != sizeof(header)) {
            break; // 连接断开或错误
        }
        
        // 这里的 header 还是网络字节序，我们需要先拷贝出来或者直接解析
        // 注意：DoIPHeader 里的字段会被 parse_header_ntoh 修改
        DoIPHeader host_header = header; 
        parse_header_ntoh(&host_header);
        
        // 读取 Payload
        std::vector<uint8_t> payload(host_header.payload_length);
        if (host_header.payload_length > 0) {
            // 需要循环读取确保读完
            size_t total_read = 0;
            while (total_read < host_header.payload_length) {
                n = recv(client_sock, payload.data() + total_read, host_header.payload_length - total_read, 0);
                if (n <= 0) break; 
                total_read += n;
            }
            if (total_read != host_header.payload_length) break;
        }

        if (host_header.payload_type == PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ) {
            uint16_t source_addr = (payload[0] << 8) | payload[1];
            std::cout << "[TCP] 收到路由激活请求，源地址: 0x" << std::hex << source_addr << std::dec << std::endl;

            // 响应 Payload: TA (2) + SA (2) + Code (1) + Reserved (4)
            std::vector<uint8_t> res_payload(9);
            res_payload[0] = (source_addr >> 8) & 0xFF; // Logical Address Tester
            res_payload[1] = source_addr & 0xFF;
            res_payload[2] = (LOGICAL_ADDRESS >> 8) & 0xFF; // Logical Address DoIP Entity
            res_payload[3] = LOGICAL_ADDRESS & 0xFF;
            res_payload[4] = 0x10; // Success
            // Reserved 0x00...

            DoIPHeader res_header;
            fill_header(&res_header, PAYLOAD_TYPE_ROUTING_ACTIVATION_RES, res_payload.size());

            send(client_sock, &res_header, sizeof(res_header), 0);
            send(client_sock, res_payload.data(), res_payload.size(), 0);
            
            is_activated = true;
            std::cout << "[TCP] 路由已激活" << std::endl;

        } else if (host_header.payload_type == PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE) {
            if (!is_activated) {
                std::cout << "[TCP] 收到诊断消息但路由未激活，忽略" << std::endl;
                continue;
            }

            uint16_t sa = (payload[0] << 8) | payload[1];
            uint16_t ta = (payload[2] << 8) | payload[3];
            
            std::cout << "[TCP] 收到诊断数据: SA=0x" << std::hex << sa << " TA=0x" << ta << std::dec << std::endl;

            // 1. 发送 Positive ACK (0x8002)
            // Payload: SA(2) + TA(2) + Code(1) + Previous UDS(optional)
            std::vector<uint8_t> ack_payload;
            ack_payload.push_back((LOGICAL_ADDRESS >> 8) & 0xFF);
            ack_payload.push_back(LOGICAL_ADDRESS & 0xFF);
            ack_payload.push_back((sa >> 8) & 0xFF);
            ack_payload.push_back(sa & 0xFF);
            ack_payload.push_back(0x00); // ACK Code

            // 把原始 UDS 数据附加上去（可选，这里加上）
            if (payload.size() > 4) {
                ack_payload.insert(ack_payload.end(), payload.begin() + 4, payload.end());
            }

            DoIPHeader ack_header;
            fill_header(&ack_header, PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK, ack_payload.size());
            
            send(client_sock, &ack_header, sizeof(ack_header), 0);
            send(client_sock, ack_payload.data(), ack_payload.size(), 0);

            // 2. 发送诊断响应 (0x8001)
            // 模拟 UDS 响应: SID + 0x40
            if (payload.size() > 4) {
                std::vector<uint8_t> uds_req(payload.begin() + 4, payload.end());
                std::vector<uint8_t> uds_res = uds_req;
                if (!uds_res.empty()) {
                    uds_res[0] += 0x40; // Simple Positive Response Simulation
                }

                std::vector<uint8_t> diag_res_payload;
                diag_res_payload.push_back((LOGICAL_ADDRESS >> 8) & 0xFF); // SA (Vehicle)
                diag_res_payload.push_back(LOGICAL_ADDRESS & 0xFF);
                diag_res_payload.push_back((sa >> 8) & 0xFF); // TA (Tester)
                diag_res_payload.push_back(sa & 0xFF);
                diag_res_payload.insert(diag_res_payload.end(), uds_res.begin(), uds_res.end());

                DoIPHeader diag_header;
                fill_header(&diag_header, PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE, diag_res_payload.size());

                send(client_sock, &diag_header, sizeof(diag_header), 0);
                send(client_sock, diag_res_payload.data(), diag_res_payload.size(), 0);
                
                std::cout << "[TCP] 发送 UDS 响应" << std::endl;
            }
        }
    }

    std::cout << "[TCP] 连接断开: " << inet_ntoa(addr.sin_addr) << std::endl;
    close(client_sock);
}

void tcp_listener() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("TCP socket creation failed");
        return;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(DOIP_PORT);

    if (bind(sockfd, (const sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("TCP bind failed");
        close(sockfd);
        return;
    }

    if (listen(sockfd, 5) < 0) {
        perror("TCP listen failed");
        close(sockfd);
        return;
    }

    std::cout << "[*] TCP 监听端口 " << DOIP_PORT << " (诊断连接)" << std::endl;

    while (true) {
        sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);
        int client_sock = accept(sockfd, (sockaddr *)&cliaddr, &len);
        if (client_sock < 0) continue;

        std::thread(handle_tcp_client, client_sock, cliaddr).detach();
    }
    close(sockfd);
}

int main() {
    std::cout << "[*] 启动 DoIP 车辆模拟器 (C++ Version)..." << std::endl;
    
    std::thread udp_thread(udp_listener);
    std::thread tcp_thread(tcp_listener);

    udp_thread.join();
    tcp_thread.join();

    return 0;
}
