import socket
import struct
import time

# DoIP 常量
PROTOCOL_VERSION = 0x02
INVERSE_PROTOCOL_VERSION = 0xFD

PAYLOAD_TYPE_VEHICLE_IDENT_REQ = 0x0001
PAYLOAD_TYPE_VEHICLE_IDENT_RES = 0x0004
PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ = 0x0005
PAYLOAD_TYPE_ROUTING_ACTIVATION_RES = 0x0006
PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE = 0x8001
PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK = 0x8002

# 诊断仪地址
TESTER_LOGICAL_ADDR = 0x0E80

def create_doip_header(payload_type, length):
    return struct.pack("!BBHL", PROTOCOL_VERSION, INVERSE_PROTOCOL_VERSION, payload_type, length)

def parse_header(data):
    return struct.unpack("!BBHL", data)

def main():
    print("=== DoIP 诊断仪模拟器 ===")
    
    # 1. 车辆发现 (UDP)
    print("\n[Step 1] 发送车辆发现请求 (UDP Broadcast)...")
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp_sock.settimeout(2.0)
    
    # 构建 Vehicle Identification Request 消息
    # Payload 为空
    header = create_doip_header(PAYLOAD_TYPE_VEHICLE_IDENT_REQ, 0)
    udp_sock.sendto(header, ('255.255.255.255', 13400))
    
    target_ip = None
    target_port = 13400
    vehicle_logical_addr = 0
    
    try:
        data, addr = udp_sock.recvfrom(1024)
        print(f"[UDP] 收到响应 来自 {addr}")
        
        ver, inv_ver, p_type, p_len = parse_header(data[:8])
        if p_type == PAYLOAD_TYPE_VEHICLE_IDENT_RES:
            payload = data[8:]
            vin = payload[:17].decode('utf-8', errors='ignore').strip('\x00')
            vehicle_logical_addr = struct.unpack("!H", payload[17:19])[0]
            print(f"       -> 发现车辆 VIN: {vin}")
            print(f"       -> 逻辑地址: 0x{vehicle_logical_addr:04X}")
            target_ip = addr[0]
            
    except socket.timeout:
        print("[!] 未发现车辆，请确保 server 正在运行。")
        # 如果是本地测试，可能广播收不到，尝试直接连接本地
        print("[*] 尝试直连 127.0.0.1 (本地回环测试)")
        target_ip = '127.0.0.1'
        vehicle_logical_addr = 0x1000 # 假设
        
    udp_sock.close()
    
    if not target_ip:
        return

    # 2. 建立 TCP 连接
    print(f"\n[Step 2] 连接车辆 TCP {target_ip}:{target_port}...")
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        tcp_sock.connect((target_ip, target_port))
    except Exception as e:
        print(f"[!] 连接失败: {e}")
        return
        
    # 3. 路由激活
    print("\n[Step 3] 发送路由激活请求...")
    # Source Address (2) + Activation Type (1, 0x00=Default) + Reserved (4)
    ra_payload = struct.pack("!HB", TESTER_LOGICAL_ADDR, 0x00) + b'\x00'*4
    ra_header = create_doip_header(PAYLOAD_TYPE_ROUTING_ACTIVATION_REQ, len(ra_payload))
    tcp_sock.sendall(ra_header + ra_payload)
    
    # 接收激活响应
    resp_header = tcp_sock.recv(8)
    ver, inv_ver, p_type, p_len = parse_header(resp_header)
    resp_payload = tcp_sock.recv(p_len)
    
    if p_type == PAYLOAD_TYPE_ROUTING_ACTIVATION_RES:
        # Logical Address Tester (2) + Logical Address DoIP Entity (2) + Response Code (1) + Reserved (4)
        ta_res, sa_res, code = struct.unpack("!HHB", resp_payload[:5])
        if code == 0x10:
            print(f"[TCP] 路由激活成功! (Code 0x{code:02X})")
        else:
            print(f"[!] 路由激活失败: 0x{code:02X}")
            return
            
    # 4. 发送诊断请求
    print("\n[Step 4] 发送诊断请求 (ReadDataByIdentifier 0x22 F1 90)...")
    # 模拟 UDS: 22 F1 90 (读取 VIN)
    uds_req = b'\x22\xF1\x90'
    
    # DoIP Payload: Source Address (2) + Target Address (2) + User Data (N)
    diag_payload = struct.pack("!HH", TESTER_LOGICAL_ADDR, vehicle_logical_addr) + uds_req
    diag_header = create_doip_header(PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE, len(diag_payload))
    tcp_sock.sendall(diag_header + diag_payload)
    
    # 5. 接收响应
    # 通常会先收到 ACK，然后是响应
    print("\n[Step 5] 等待响应...")
    
    while True:
        header_data = tcp_sock.recv(8)
        if not header_data:
            break
        
        ver, inv_ver, p_type, p_len = parse_header(header_data)
        payload_data = tcp_sock.recv(p_len)
        
        if p_type == PAYLOAD_TYPE_DIAGNOSTIC_POS_ACK:
            print(f"[TCP] 收到 DoIP ACK (Positive)")
        elif p_type == PAYLOAD_TYPE_DIAGNOSTIC_MESSAGE:
            # 解析 UDS 响应
            sa, ta = struct.unpack("!HH", payload_data[:4])
            uds_res = payload_data[4:]
            print(f"[TCP] 收到诊断响应: {uds_res.hex().upper()}")
            print(f"       -> 来自: 0x{sa:04X} 发给: 0x{ta:04X}")
            break # 演示结束

    print("\n=== 演示结束 ===")
    tcp_sock.close()

if __name__ == "__main__":
    main()
