# DoIP (Diagnostics over IP) 学习指南 (C++ 版)

欢迎来到 DoIP 学习项目！DoIP (ISO 13400) 是一种通过以太网（IP 网络）对车辆进行诊断的协议。

## 什么是 DoIP？

DoIP 允许外部测试设备（如诊断仪）通过以太网与车辆内部的 ECU（电子控制单元）进行通信。相比传统的 CAN 总线诊断，DoIP 速度更快，适合传输大量数据（如刷写软件）。

## 核心流程

1.  **车辆发现 (Vehicle Discovery)**:
    -   诊断仪发送 UDP 广播消息。
    -   车辆回复车辆信息（VIN, 逻辑地址等）。
    -   默认端口：UDP 13400。

2.  **建立连接**:
    -   诊断仪与车辆建立 TCP 连接。
    -   默认端口：TCP 13400。

3.  **路由激活 (Routing Activation)**:
    -   在发送诊断请求前，必须先激活路由。
    -   相当于“登录”或“握手”。

4.  **诊断通信**:
    -   通过 TCP 发送 UDS (Unified Diagnostic Services) 诊断命令（如读取故障码、读取数据流）。

## DoIP 消息格式

一个 DoIP 消息由 **头部 (Header)** 和 **载荷 (Payload)** 组成：

| 字段 | 长度 (Bytes) | 说明 |
| :--- | :--- | :--- |
| Protocol Version | 1 | 协议版本 (例如 0x02) |
| Inverse Protocol Version | 1 | 版本的按位取反 (例如 0xFD) |
| Payload Type | 2 | 消息类型 (例如 0x0001 = 车辆声明) |
| Payload Length | 4 | 后面载荷数据的长度 |
| Payload Data | N | 具体的数据内容 |

## 项目结构

-   `doip_server.cpp`: 模拟一辆车（DoIP 实体），负责响应发现请求和处理诊断命令。
-   `doip_client.cpp`: 模拟一个诊断仪（External Test Equipment），负责发起连接和发送诊断请求。
-   `doip_common.h`: 定义 DoIP 协议头部结构和常量。
-   `Makefile`: 项目编译脚本。

## 如何运行

你需要两个终端窗口来分别运行服务端（车）和客户端（诊断仪）。

### 1. 编译代码

在终端中输入 `make` 进行编译：
```bash
make
```
这将生成 `doip_server` 和 `doip_client` 两个可执行文件。

### 2. 运行服务端 (车辆)

在一个终端中运行：
```bash
./doip_server
```
输出示例：
```
[*] 启动 DoIP 车辆模拟器 (C++ Version)...
[*] UDP 监听端口 13400 (车辆发现)
[*] TCP 监听端口 13400 (诊断连接)
```

### 3. 运行客户端 (诊断仪)

在另一个终端中运行：
```bash
./doip_client
```
客户端会自动执行以下步骤：
1.  发送 UDP 广播发现车辆。
2.  建立 TCP 连接。
3.  发送路由激活请求。
4.  发送一个模拟的 UDS 请求 (0x22 读取数据)。
5.  显示车辆回复的 UDS 响应。

### 预期输出示例

**Client 端:**
```
=== DoIP 诊断仪模拟器 (C++ Version) ===

[Step 1] 发送车辆发现请求 (UDP Broadcast)...
[UDP] 收到响应 来自 127.0.0.1
       -> 发现车辆 VIN: DOIPDEMOCAR12345
       -> 逻辑地址: 0x1000

[Step 2] 连接车辆 TCP 127.0.0.1:13400...

[Step 3] 发送路由激活请求...
[TCP] 路由激活成功! (Code 0x10)

[Step 4] 发送诊断请求 (ReadDataByIdentifier 0x22 F1 90)...

[Step 5] 等待响应...
[TCP] 收到 DoIP ACK (Positive)
[TCP] 收到诊断响应 SA=0x1000 TA=0xe80
       -> UDS Data: 62 F1 90 

=== 演示结束 ===
```

## 清理编译文件
```bash
make clean
```
