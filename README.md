# RemoteControl

> 一个基于 Windows API 的简易远程桌面控制系统（学习项目）。

**客户端** 实时显示远程桌面画面，并将本地鼠标、键盘事件转发至服务端；  
**服务端** 执行截屏、模拟键鼠操作。  
项目采用自定义二进制协议通信，支持粘包处理和多线程并发。

---

## 📋 目录

- [技术栈与依赖](#技术栈与依赖)
- [系统架构](#系统架构)
- [功能清单](#功能清单)
- [关键技术点](#关键技术点)
- [编译与运行](#编译与运行)
- [目录结构](#目录结构)
- [已知问题](#已知问题)
- [许可证](#许可证)

---

## 🛠️ 技术栈与依赖

### 编程语言
- **C** / **C++**（使用少量面向对象特性）

### 核心 API
| 模块         | 使用的 Windows API / 技术                               | 用途                         |
|--------------|--------------------------------------------------------|------------------------------|
| 网络通信     | Winsock2 (`ws2_32`)                                    | TCP 连接、数据收发           |
| 图像处理     | `CImage`（ATL）<br>GDI+ 流（`IStream`）                | 屏幕截图、PNG 压缩、渲染     |
| 屏幕截图     | `BitBlt`、`GetDC`                                     | 捕获桌面画面                 |
| 输入模拟     | `mouse_event`、`SendInput`                            | 模拟鼠标键盘操作             |
| 线程同步     | `CRITICAL_SECTION`（客户端）                          | 保护图像数据的线程安全       |
| 线程消息     | `CreateThread`、`PostThreadMessage`、`GetMessage`     | 工作线程异步处理任务         |
| DPI 适配     | `SetProcessDpiAwarenessContext`                       | 高 DPI 下正确获取分辨率      |
| 内存管理     | `GlobalAlloc`、`CreateStreamOnHGlobal`                | 构建 GDI+ 可用的内存流       |

### 编译环境
- Windows 10 / 11
- Visual Studio 2019 或更高版本（Desktop development with C++）
- 无任何第三方库依赖

---


### 客户端架构
主线程 (WinMain)
├─ 初始化窗口、连接服务器
├─ 创建屏幕工作线程 ── 循环：请求截图 → 接收 → 渲染
└─ 消息循环 (winProc) ── 捕获鼠标/键盘事件 → 封装发送


- **屏幕显示**：收到 PNG 数据后通过 `CreateStreamOnHGlobal` + `CImage::Load` 还原，在 `WM_PAINT` 中拉伸绘制。
- **坐标映射**：将窗口客户区坐标等比映射到远程真实分辨率（`rx = x * remote_width / client_width`）。

### 服务端架构
主线程
├─ 监听端口、接受连接
├─ 循环 recv() → 循环解包（处理粘包）
└─ 根据 cmdID 将包投递到对应工作线程
├─ 屏幕线程 (HandleScreenThreadFuc)
├─ 鼠标线程 (HandleMouseThreadFuc)
└─ 键盘线程 (HandleKeyBoardThreadFuc)

text
- **线程间通信**：使用 `PostThreadMessage` + 自定义消息（`WM_HANDLE_*`）。
- **任务分发**：`HandleCommand()` 根据命令 ID 将数据包指针作为 lParam 投递。

### 通信协议
自定义二进制协议，紧凑对齐（1字节对齐）：
#pragma pack(push, 1)
struct PacketHeader {
    int magic;      // 魔数: 0x55AA77CC
    int cmdID;      // 命令类型 (1:屏幕 2:鼠标 3:键盘)
    int body_len;   // 数据体长度
};
struct Packet {
    PacketHeader header;
    char body[];    // 变长数据体
};
#pragma pack(pop)


数据体结构：

鼠标：int action + POINT ptXY（常用操作及坐标）

键盘：int virtual_code + int key_status（0按下/1松开）

✅ 功能清单
客户端
TCP 连接服务端
定时请求远程屏幕，接收并显示
窗口缩放时自动拉伸画面（HALFTONE 高质量缩放）
鼠标移动节流（20ms最少间隔）
支持鼠标移动、左/右/中键按下/松开/双击、滚轮
支持键盘所有按键（含系统键）按下/松开
坐标比例自适应映射

服务端
监听端口，接受客户端连接
完整桌面截图（BitBlt）并转换为 PNG 传输
高 DPI 感知与真实分辨率获取
模拟鼠标操作（移动 + 点击/双击/滚轮）
模拟键盘操作（SendInput）
粘包处理（循环解包 + 缓冲区移位）
多线程任务处理（屏幕、鼠标、键盘独立线程）

🔑 关键技术点
1. 粘包与半包处理（服务端）
TCP 流式传输可能导致一次 recv 收到多个包或半个包。
实现方式：

使用动态缓冲区 recv_buffer 与 bufferData_len 记录已接收的数据。

循环调用 ParsePacket() 尝试从缓冲区头部提取一个完整包（内部通过魔数定位 + 长度校验）。

提取成功后，使用 memmove 将缓冲区剩余数据前移，更新 bufferData_len。

若剩余数据不足一个包则退出循环，等待下一次 recv。

2. 多线程消息模型（服务端）
三个工作线程运行各自的 GetMessage 循环，主线程通过 PostThreadMessage 分发任务，避免阻塞主网络循环。
注意：主线程在创建线程后立即使用 Sleep(200) 等待线程消息队列就绪，简单但不优雅，生产环境可用事件同步。

3. 屏幕截图与图像传输流程（服务端）
GetDC(NULL) → 屏幕 HDC
    ↓
CImage::Create + BitBlt  → 捕获位图
    ↓
GlobalAlloc + CreateStreamOnHGlobal → 创建 IStream
    ↓
CImage::Save (Gdiplus::ImageFormatPNG) → PNG 压缩到流
    ↓
GlobalLock 获取指针 → PackPacket → send
所有资源均在函数结束时释放，防止泄漏。

4. 键鼠模拟（服务端）
鼠标：先 SetCursorPos 移动到目标坐标，再根据 action 类型调用 mouse_event（部分双击通过两次点击 + Sleep 实现）。

键盘：构造 INPUT 结构体，通过 SendInput 模拟按键（按下或松开）。

5. DPI 适配（服务端）
调用 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) 使程序获得真实屏幕分辨率，避免高缩放率下截图尺寸错误。

6. 图像线程安全（客户端）
g_image 同时被屏幕线程和 UI 线程（WM_PAINT）访问，使用 CRITICAL_SECTION 保护加载与绘制过程。

⚙️ 编译与运行
1. 环境准备
操作系统：Windows 10/11
编译器：Visual Studio 2019+（包含 ATL 和 Windows SDK）

2. 配置代码
打开 Client/main.cpp 和 Server/main.cpp，修改服务端 IP 地址：

cpp
// Server: InitAndListen_ServerSocket()
inet_addr("你的服务端IP");

// Client: InitAndConnect_ClientSocket()
inet_addr("你的服务端IP");
3. 编译
新建两个空的控制台或桌面应用程序项目，分别添加对应 .cpp 文件。
确保链接器依赖项包含 ws2_32.lib（代码中已 pragma 声明）。

编译生成 Server.exe 和 Client.exe。

4. 运行
在被控机器上运行 Server.exe。
在控制端运行 Client.exe。
客户端窗口将显示远端桌面，在窗口内操作即可控制服务端。

⚠️ 已知问题
      问题	                       说明
客户端粘包处理缺失	屏幕接收线程仅解析一个包，极端情况可能丢帧
服务端线程退出不优雅	主线程退出时未通知工作线程，可能导致进程残留
鼠标双击间隔固定	使用 Sleep(50)，未适配系统双击速度设置
无身份验证与加密	仅适用于可信网络，不可直接暴露于公网
高 DPI 下物理尺寸获取	使用 GetSystemMetrics，在多显示器不同缩放比下可能不准确
