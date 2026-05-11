--------------------------------------------------服务端(被控端)部分--------------------------------------------------\
头文件:给编译器提供函数/结构体/常量的声明(无函数功能的实现，只负责声明)\
#include <stdio.h>:C/C++标准库头文件，支持跨平台(Windows/Linux/mac)，提供基础的控制台、文件操作函数\
#include <Windows.h>:Windows平台最重要的头文件，包含Windows操作系统90%的原生功能声明:网络、线程、窗口、消息循环、输入模拟、屏幕绘图、内存管理等\
TCP 网络通信:socket/bind/listen/accept/recv/send\
多线程:CreateThread/PostThreadMessage/GetMessage\
鼠标模拟:SetCursorPos/mouse_event\
键盘模拟:SendInput\
屏幕绘图:GetDC/ReleaseDC/BitBlt（截图基础）\
数据类型:SOCKET/POINT/INPUT/MSG结构体\
#include <atlimage.h>:封装GDI/GDI+ 图像操作，提供CImage类，简化屏幕截图、图片编码、保存、格式转换\
#include <ShellScalingApi.h>:提供高DPI屏幕适配API，解决 2K/4K高分屏的界面失真、截图尺寸错误\

链接库:给链接器提供功能的实现代码(即头文件中声明的函数实现)\
#pragma comment(lib,"ws2_32.lib")\

宏定义:纯文本替换\
接收缓冲区大小宏\
#define RECV_BUFFER_LEN 1024*1024\
自定义Windows线程消息\
#define WM_HANDLE_SCREEN (WM_USER + 1)\
#define WM_HANDLE_MOUSE (WM_USER + 2)\
#define WM_HANDLE_KEYBOARD (WM_USER + 3)\
#define WM_HANDLE_INVOKE_MSG_LOOP (WM_USER + 4)\

WM_USER:Windows系统自带的常量(是固定数值1024)\
0~WM_USER-1(0~1023)是系统内置消息序号，0~1023分别代表Windows不同的系统消息\
≥WM_USER(1024及以上)是可用于自定义消息的序号标注\

内存对齐:CPU读取内存时，为了提高读取效率会一次性读取4/8字节(32/64位系统)，所以编译器为了迎合CPU会自动在结构体里插入空白填充字节实现数据对齐\
结构体内存对齐规则:\
规则1:第一个成员从偏移地址0开始\
结构体的第一个变量，直接放在结构体的起始位置\
规则2:后续成员偏移地址必须是自身大小的整数倍\
如果当前位置不满足，编译器会自动插入空白字节填充直到对齐\
规则3:结构体总大小必须是有效对齐值(默认=结构体中最大的成员类型大小；如果写了#pragma pack(n)，就取n)的整数倍
所有成员放完后，如果总长度不满足末尾再填充空白字节\
struct PacketHeader{\
	int magic;         
	int cmd;            
	int body_len;      
};\
这个结构体因为成员全为int类型所以无需填充空白字节，所以#pragma pack(push,1)和#pragma pack(push,1)的有无并没有影响，但为了规范建议加上，倘若稍加改动比如下面这个就必须要有#pragma pack(push,1)和#pragma pack(push,1)\
struct PacketHeader{\
    char flag;                
	int magic;         
	int cmd;            
	int body_len;      
};\
无#pragma pack(push,1)和#pragma pack(push,1):采用默认对齐规则，flag占第0字节，1,2,3字节填充空白，magic占4~7字节，cmdID占8~11字节，body_len占12~15字节，共16字节，是该结构体最大的成员类型大小(int 4字节)整数倍无需继续填充\
有#pragma pack(push,1)和#pragma pack(push,1):采用1字节对齐，占13字节，无空白填充\

cmdID可取值:\
enum CMD {\
	CMD_SCREEN = 1,\
	CMD_MOUSE = 2,\
	CMD_KEYBOARD = 4,\
	CMD_TESTCONNECT = 2026\
};\

鼠标信息:\
enum ENUM_MOUSE {
	MOUSE_MOVE = 1,     //鼠标移动\
	MOUSE_LDOWN = 2,    //鼠标左键按下\
	MOUSE_LUP = 3,      //鼠标左键抬起\
	MOUSE_RDOWN = 4,    //鼠标右键按下\
	MOUSE_RUP = 5,      //鼠标右键抬起\
	MOUSE_MDOWN = 6,    //鼠标中间按下\
	MOUSE_MUP = 7,      //鼠标中间抬起\
	MOUSE_LCLICK = 8,   //鼠标左键双击\
	MOUSE_RCLICK = 9,   //鼠标右键双击\
	MOUSE_MCLICK = 10,  //鼠标中间双击\
	MOUSE_LDCLICK = 11, //鼠标左键双击\
	MOUSE_RDCLICK = 12, //鼠标右键双击\
	MOUSE_MDCLICK = 13  //鼠标中间双击\
};\
struct Mouse {\
	int action;         //鼠标行为\
	POINT ptXY;         //鼠标坐标\
};\

键盘信息:\
struct KeyBoard {\
	int virtual_code;   //虚拟码\
	int key_status;     // 0按下 / 1松开\
};\

InitServerSocket():初始化Windows平台的TCP服务端套接字，执行后服务端就会在指定IP和端口上等待客户端连接\
int InitServerSocket() {\
	//1.初始化网络环境\
	WSADATA wsadata;\
	WSAStartup(MAKEWORD(2, 2), &wsadata);\
	//2.创建服务器socket(AF_INET:IPv4,SOCK_STREAM:TCP协议,SOCK_DGRAM:UDP协议)\
	g_server_socket = socket(AF_INET, SOCK_STREAM, 0);\
	if (g_server_socket == INVALID_SOCKET) {\
		printf("服务器创建socket失败\r\n");\
		return -1;\
	}\
	//3.为服务器socket绑定地址\
	SOCKADDR_IN server_addr;\
	server_addr.sin_family = AF_INET;                                //标识地址使用IPv4协议\
	server_addr.sin_port = ntohs(9999);                              //监听端口，取值0-65535(IPv4为32位)，ntohs将9999转为网络字节序\
	server_addr.sin_addr.S_un.S_addr = inet_addr("192.168.16.129");   //本机ip\
	if (bind(g_server_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {\
		printf("服务器socket绑定地址失败\r\n");\
		return -2;\
	}\
	//4.启动服务器socket监听(第二个参数backlog:允许完成三次握手的客户端数量)\
	if (listen(g_server_socket, 1) == SOCKET_ERROR) {\
		printf("服务器启动监听失败\r\n");\
		return -3;\
	}\
	return 0;\
}\
第一步操作:\
WSADATA:Windows Socket库预定义的结构体，专门存储Winsock库的初始化状态信息\
WSAStartup:Windows Socket规范规定的唯一初始化函数,为当前进程加载Winsock库、初始化网络环境\
MAKEWORD:系统宏，将两个8位数字拼接为16位整数\
2, 2:指定使用Winsock 2.2版本\
&wsadata:将实际加载的Winsock版本、库状态等信息写入该结构体变量\
第二步操作:\
socket():向操作系统申请创建一个套接字，分配对应的内核资源，c成功返回非负整数(套接字句柄，用于后续绑定、监听、收发数据)，失败返回INVALID_SOCKET(宏，固定值-1)\
AF_INET:IPv4\
AF_INET+SOCK_STREAM+0→TCP\
AF_INET+SOCK_DGRAM+0→UDP\
第三步操作:\
SOCKADDR_IN:Windows专用IPv4套接字地址结构体,存储服务端需要绑定的协议族、端口号、IP地址\
sin_family:结构体的地址家族字段\
AF_INET:强制指定该地址使用IPv4\
sin_port:结构体的16位端口号字段，存储服务端监听端口\
ntohs():网络字节序 → 主机字节序，用于读取网络传来的端口\
htons():主机字节序 → 网络字节序，用于端口绑定/监听/连接\
sin_addr:存放IP的区域\
S_un:Windows规定的中间过渡格\
S_addr:最终存IP的格子\
inet_addr():将点分十进制字符串转换为32位网络字节序整数
bind():将套接字与IP地址+端口号进行内核级绑定，成功返回0失败返回SOCKET_ERROR\
g_server_socket:要绑定的套接字\
(sockaddr*):强制类型转换为通用结构体sockaddr\
server_addr:提前填好的IPv4地址结构体(包含 IP、端口、协议)\
sizeof(SOCKADDR_IN):地址结构体长度\
第四步操作:\
listen():开启监听，成功返回0失败返回SOCKET_ERROR\
g_server_socket:要进行监听的套接字\
1:是listen()的第二个参数backlog表示允许连接的客户端数量\

等待客户端连接并阻塞执行:\
SOCKADDR_IN client_addr;\
int client_addr_len = sizeof(SOCKADDR_IN);
printf("等待客户端连接\r\n");\
g_client_socket = accept(g_server_socket, (sockaddr*)&client_addr, &client_addr_len);\
printf("客户端连接成功\r\n");\
client_addr:保存与服务器连接的客户端的IP、端口、协议\
g_client_socket:与服务器连接的客户端socket\
listen()与accept():必须先listen()将g_server_socket设置为监听状态(允许客户端发送TCP连接请求，但仅排队，不建立正式连接),再accept()阻塞等待 → 从队列中取出一个已完成TCP三次握手的客户端，accept()返回值得到与服务器通信的g_client_socket\
g_server_socket:只负责服务端监听+接受客户端连接(listen()+accept())\
g_client_socket:只负责服务端和客户端的数据传输(send()+recv())\

PackPacket():实现自定义网络/通信协议数据包封装，核心作用是将命令ID、数据长度、原始数据组装成一个固定格式的Packet数据包，分配堆内存后返回数据包指针\
Packet* PackPacket(int cmdID, int body_len, char* buffer) {\
	Packet* packet = (Packet*)malloc(sizeof(PacketHeader) + body_len);\
	memset(packet, 0, (sizeof(PacketHeader) + body_len));\
	packet->header.magic = 0x55AA77CC;\
	packet->header.cmdID = cmdID;\
	packet->header.body_len = body_len;\
	memcpy(packet->body, buffer, body_len);\
	return packet;\
}\
malloc():分配堆内存\
memset():将分配的内存块清零避免脏数据\
memcpy(packet->body, buffer, body_len):内存拷贝，前两个参数都要是地址\
第1个参数:目标地址\
第2个参数:源地址\
第3个参数:复制多少字节\

ParsePacket():从TCP接收的连续数据流中拆分出一个完整的Packet数据包\
Packet* ParsePacket(char* buffer, int len) {\
	PacketHeader pckHeader;\
	Packet* pck;\
	int index = 0;\
	for (; index < len; index++)\
	{\
		if (*(int*)(buffer + index) == 0x55AA77CC)\
		{\
			pckHeader.magic = *(int*)(buffer + index);\
			index += 4;\
			break;\
		}\
	}\
	pckHeader.cmdID = *(int*)(buffer + index);\
	index += 4;\
	pckHeader.body_len = *(int*)(buffer + index);\
	index += 4;\
	if (pckHeader.body_len == 0) {\
		pck = (Packet*)malloc(sizeof\(PacketHeader));\
		memcpy(&pck->header, &pckHeader, sizeof(PacketHeader));\
		return pck;\
	}\
	else if (pckHeader.body_len > 0) {\
		pck = (Packet*)malloc(sizeof(PacketHeader) + pckHeader.body_len);\
		memcpy(&pck->header, &pckHeader, sizeof(PacketHeader));\
		memcpy(pck->body, buffer + index, pckHeader.body_len);\
		return pck;\
	}\
	return NULL;\
}\

GetPacketLen():获得数据包长度\
int GetPacketLen(Packet* pck) {\
	if (pck != NULL){\
		return sizeof(PacketHeader) + pck->header.body_len;\
	}\
}\

多线程实现:以screen线程为例，mouse,keyboard同理\
第一部分:初始赋值全局线程ID为0\
unsigned long g_screen_thread_id = 0;\
作用:存储工作线程的唯一ID\
第二部分:线程函数\
DWORD WINAPI HandleScreenThreadFuc(LPVOID lpThreadParameter) {\
	MSG msg;\
	while (GetMessage(&msg, 0, 0, 0)) {\
		if (msg.message == WM_HANDLE_SCREEN) {\
			Packet* packet = (Packet*)msg.lParam;\
			HandleScreen(packet);\
			free(packet);\
		}\
	}\
	return 0;\
}\
DWORD:函数返回值类型，等价于unsigned long，线程退出时返回状态码(这里返回0)\
WINAPI:Windows标准调用约定，是系统调用线程函数的固定规则\
LPVOID lpThreadParameter:LPVOID=void*,该参数用于接收CreateThread第4个参数传入的数据\
MSG msg:Windows消息结构体变量,存储主线程发送给这个线程的消息内容\
GetMessage():Windows系统API，阻塞式函数，没有消息时线程休眠，不占用CPU；收到消息时返回TRUE并将消息写入msg；收到退出消息时返回FALSE，退出线程。三个0参数分别代表监听所有窗口的消息；监听所有消息类型；无过滤，接收全部消息\
msg.lParam:存储的是主线程传过来的数据包的内存地址，即
PostThreadMessage(g_mouse_thread_id, WM_HANDLE_KEYBOARD, NULL, (LPARAM)pck)的pck的内存地址\
第三部分:创建线程\
CreateThread(NULL, 0, HandleScreenThreadFuc, NULL, 0, &g_screen_thread_id);\
CreateThread原型:HANDLE CreateThread(\
    LPSECURITY_ATTRIBUTES lpThreadAttributes,//安全属性\
    SIZE_T                dwStackSize,       //栈大小\
    LPTHREAD_START_ROUTINE lpStartAddress,   //线程入口函数\
    LPVOID                lpParameter,       //传递给线程的参数\
    DWORD                 dwCreationFlags,   //创建标志\
    LPDWORD               lpThreadId         //输出线程ID\
);\
NULL, 0, HandleScreenThreadFuc, NULL, 0, &g_screen_thread_id分别代表(使用系统默认的线程安全属性)，(使用系统默认的线程栈大小(和主线程一致))，(线程入口函数:线程启动后会立即执行这个函数)，(不向线程函数传递任何自定义参数)，(线程创建后立即开始运行)，(系统自动生成的线程ID(自动写入g_screen_thread_id))\
第四部分：发送空消息唤醒线程\
PostThreadMessage(g_screen_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);\
PostThreadMessage(g_mouse_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);\
PostThreadMessage(g_keyboard_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);\



















