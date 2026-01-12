import socket
import struct
import threading
import time

# DoIP 常量定义
PROTOCOL_VERSION = 0x02
INVERSE_PROTOCOL_VERSION = 0xFD

# Payload Types
PAYLOAD_TYPE_VEHICLE_IDENT_REQ = 0x0001
PAYLOAD_TYPE_VEHICLE_IDENT_RES = 0x0004  # ISO 13400-2:2012 define 0x0004 as Vehicle Announcement/Identification Response
PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ = 0x0005
PAYLOAD_TYPE_ROUTING_ACTIVATION_RES = 0x0006
PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE = 0x8001
PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK = 0x8002
PAYLOAD_TYPE_DIAGNOSTIC_NEG_ACK = 0x8003

# 模拟车辆信息
VIN = b'DOIPDEMOCAR12345'  # 17 bytes usually, here 16 for simplicity + padding if needed
LOGICAL_ADDRESS = 0x1000

def create_doip_header(payload_type, length):
    return struct.pack("!BBHL", PROTOCOL_VERSION, INVERSE_PROTOCOL_VERSION, payload_type, length)

class DoIPServer:
    def __init__(self, host='0.0.0.0', port=13400):
        self.host = host
        self.port = port
        self.running = True
        
        # 记录已激活的客户端
        self.active_clients = set()

    def start(self):
        print(f"[*] 启动 DoIP 车辆模拟器...")
        
        # 启动 UDP 监听 (车辆发现)
        self.udp_thread = threading.Thread(target=self.udp_listener)
        self.udp_thread.daemon = True
        self.udp_thread.start()
        
        # 启动 TCP 监听 (诊断通信)
        self.tcp_thread = threading.Thread(target=self.tcp_listener)
        self.tcp_thread.daemon = True
        self.tcp_thread.start()
        
        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n[*] 停止服务器...")
            self.running = False

    def udp_listener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        print(f"[*] UDP 监听端口 {self.port} (车辆发现)")

        while self.running:
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) < 8:
                    continue
                
                # 解析头部
                ver, inv_ver, p_type, p_len = struct.unpack("!BBHL", data[:8])
                
                if p_type == PAYLOAD_TYPE_VEHICLE_IDENT_REQ:
                    print(f"[UDP] 收到车辆发现请求 来自 {addr}")
                    # 构建车辆声明响应
                    # VIN (17) + Logical Address (2) + EID (6) + GID (6) + Further Action (1) + Sync Status (1)
                    # 这里简化回复，只包含 VIN 和逻辑地址
                    # 注意：标准响应结构比较复杂，这里为了演示简化处理
                    payload = VIN.ljust(17, b'\x00') + struct.pack("!H", LOGICAL_ADDRESS) + b'\x00'*6 + b'\x00'*6 + b'\x00'
                    
                    header = create_doip_header(PAYLOAD_TYPE_VEHICLE_IDENT_RES, len(payload))
                    sock.sendto(header + payload, addr)
                    
            except Exception as e:
                print(f"[UDP Error] {e}")

    def tcp_listener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        sock.listen(5)
        print(f"[*] TCP 监听端口 {self.port} (诊断连接)")

        while self.running:
            client_sock, addr = sock.accept()
            print(f"[TCP] 新连接: {addr}")
            client_thread = threading.Thread(target=self.handle_tcp_client, args=(client_sock, addr))
            client_thread.daemon = True
            client_thread.start()

    def handle_tcp_client(self, client_sock, addr):
        is_activated = False
        try:
            while self.running:
                header_data = client_sock.recv(8)
                if not header_data:
                    break
                
                ver, inv_ver, p_type, p_len = struct.unpack("!BBHL", header_data)
                
                # 读取 Payload
                payload_data = b''
                while len(payload_data) < p_len:
                    chunk = client_sock.recv(p_len - len(payload_data))
                    if not chunk:
                        break
                    payload_data += chunk
                
                if p_type == PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ:
                    # 路由激活请求
                    # Payload: Source Address (2) + Activation Type (1) + Reserved (4)
                    source_addr = struct.unpack("!H", payload_data[:2])[0]
                    print(f"[TCP] 收到路由激活请求，源地址: 0x{source_addr:04X}")
                    
                    # 响应: Logical Address Tester (2) + Logical Address DoIP Entity (2) + Response Code (1) + Reserved (4)
                    # Response Code 0x10 = Success
                    res_payload = struct.pack("!HHB", source_addr, LOGICAL_ADDRESS, 0x10) + b'\x00\x00\x00\x00'
                    header = create_doip_header(PAYLOAD_TYPE_ROUTING_ACTIVATION_RES, len(res_payload))
                    client_sock.sendall(header + res_payload)
                    is_activated = True
                    print("[TCP] 路由已激活")
                    
                elif p_type == PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE:
                    if not is_activated:
                        print("[TCP] 收到诊断消息但路由未激活，忽略")
                        # 实际应该发送否定响应
                        continue
                        
                    # 诊断消息
                    # Payload: Source Address (2) + Target Address (2) + User Data (N)
                    sa, ta = struct.unpack("!HH", payload_data[:4])
                    uds_data = payload_data[4:]
                    print(f"[TCP] 收到诊断数据: SA=0x{sa:04X} TA=0x{ta:04X} UDS={uds_data.hex()}")
                    
                    # 首先发送正向确认 (Positive ACK)
                    # Payload: Source Address (2) + Target Address (2) + ACK Code (1) + Previous Diag Message (N, optional)
                    # ACK Code 0x00 = ACK
                    ack_payload = struct.pack("!HHB", LOGICAL_ADDRESS, sa, 0x00) + uds_data
                    header_ack = create_doip_header(PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK, len(ack_payload))
                    client_sock.sendall(header_ack)
                    
                    # 模拟 UDS 响应 (简单的 echo + 0x40，模拟 Positive Response)
                    # 假设 UDS 是 SID + SubFunc...
                    # 响应通常是 (SID + 0x40) + Data
                    if len(uds_data) > 0:
                        sid = uds_data[0]
                        response_uds = bytes([sid + 0x40]) + uds_data[1:] # 简单的肯定响应模拟
                        
                        # 构建诊断消息响应
                        # DoIP Payload: Source (Vehicle) -> Target (Tester)
                        res_diag_payload = struct.pack("!HH", LOGICAL_ADDRESS, sa) + response_uds
                        header_diag = create_doip_header(PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE, len(res_diag_payload))
                        client_sock.sendall(header_diag)
                        print(f"[TCP] 发送 UDS 响应: {response_uds.hex()}")

        except Exception as e:
            print(f"[TCP Error] {e}")
        finally:
            client_sock.close()
            print(f"[TCP] 连接断开: {addr}")

if __name__ == "__main__":
    server = DoIPServer()
    server.start()
