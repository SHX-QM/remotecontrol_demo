#include <stdio.h>
#include <Windows.h>
#include <atlimage.h>
#include <ShellScalingApi.h>
#pragma comment(lib,"ws2_32.lib")
//在InitAndListen_ServerSocket中更改10.8.143.58为自己服务器IPv4地址
#define RECV_BUFFER_LEN 1024*1024
#define WM_HANDLE_SCREEN (WM_USER + 1)
#define WM_HANDLE_MOUSE (WM_USER + 2)
#define WM_HANDLE_KEYBOARD (WM_USER + 3)
#define WM_HANDLE_INVOKE_MSG_LOOP (WM_USER + 4)

enum CMD {
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 3
};

#pragma pack(push,1)
struct PacketHeader {
	int magic;
	int cmdID;
	int body_len;
};
#pragma pack(pop)

struct Packet {
	PacketHeader header;
	char body[];
};

enum ENUM_MOUSE {
	MOUSE_MOVE = 1,						//鼠标移动
	MOUSE_LDOWN = 2,					//鼠标左键按下
	MOUSE_LUP = 3,						//鼠标左键抬起
	MOUSE_RDOWN = 4,					//鼠标右键按下
	MOUSE_RUP = 5,						//鼠标右键抬起
	MOUSE_MDOWN = 6,					//鼠标中间按下
	MOUSE_MUP = 7,						//鼠标中间抬起
	MOUSE_LCLICK = 8,					//鼠标左键单击
	MOUSE_RCLICK = 9,					//鼠标右键单击
	MOUSE_MCLICK = 10,					//鼠标中间单击
	MOUSE_LDCLICK = 11,					//鼠标左键双击
	MOUSE_RDCLICK = 12,					//鼠标右键双击
	MOUSE_MDCLICK = 13,					//鼠标中间双击
	MOUSE_WHEEL_UP = 14,                //滚轮向上
	MOUSE_WHEEL_DOWN = 15               //滚轮向下
};

struct Mouse {
	int action;							//鼠标行为
	POINT ptXY;							//鼠标坐标
};

struct KeyBoard {
	int virtual_code;					//虚拟码
	int key_status;						// 0按下 / 1松开
};

SOCKET g_server_socket;										    //负责监听和接受连接
SOCKET g_client_socket;								            //负责和客户端进行数据交换
unsigned long g_screen_thread_id = 0;                           //屏幕线程ID
unsigned long g_mouse_thread_id = 0;                            //鼠标线程ID
unsigned long g_keyboard_thread_id = 0;						    //键盘线程ID
int InitAndListen_ServerSocket();							    //初始化服务器socket并启动监听
Packet* PackPacket(int cmdID, int body_len, char* buffer);	    //打包数据
Packet* ParsePacket(char* buffer, int len);					    //从tcp流读出一个完整数据包
int GetPacketLen(Packet* pck);								    //获取数据包长度
DWORD WINAPI HandleScreenThreadFuc(LPVOID lpThreadParameter);   //屏幕线程函数
DWORD WINAPI HandleMouseThreadFuc(LPVOID lpThreadParameter);    //鼠标线程函数
DWORD WINAPI HandleKeyBoardThreadFuc(LPVOID lpThreadParameter); //键盘线程函数
void HandleCommand(Packet* pck);                                //根据消息类型将数据包分发给不同线程处理
int HandleScreen(Packet* pck);                                  //服务端截图并将截图数据转换为二进制发送给客户端
int HandleMouse(Packet* pck);                                   //模拟鼠标操作
int HandleKeyboard(Packet* pck);                                //模拟键盘操作

int main() {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);//让程序识别真实的屏幕分辨率

	if (InitAndListen_ServerSocket() != 0) {
		return 0;
	}

	CreateThread(NULL, 0, HandleScreenThreadFuc, NULL, 0, &g_screen_thread_id);
	CreateThread(NULL, 0, HandleMouseThreadFuc, NULL, 0, &g_mouse_thread_id);
	CreateThread(NULL, 0, HandleKeyBoardThreadFuc, NULL, 0, &g_keyboard_thread_id);

	PostThreadMessage(g_screen_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);
	PostThreadMessage(g_mouse_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);
	PostThreadMessage(g_keyboard_thread_id, WM_HANDLE_INVOKE_MSG_LOOP, NULL, NULL);
	Sleep(200);

	SOCKADDR_IN client_addr;
	int client_addr_len = sizeof(SOCKADDR_IN);
	printf("等待客户端连接\r\n");
	g_client_socket = accept(g_server_socket, (sockaddr*)&client_addr, &client_addr_len);//等待客户端连接并返回客户端socket,会阻塞在这里等待客户端连接
	printf("客户端连接成功\r\n");

	char* recv_buffer = (char*)malloc(RECV_BUFFER_LEN);
	if (recv_buffer == NULL) {
		printf("接收缓冲区分配失败\r\n");
		return -1;
	}
	static int bufferData_len = 0;
	while (true)
	{
		int len = recv(g_client_socket, recv_buffer + bufferData_len, RECV_BUFFER_LEN - bufferData_len, 0);//tcp流式传输，一次接收到的数据可能是数个包(存在半包情况)
		if (len > 0) {
			bufferData_len += len;
		}
		else {
			break;
		}

		while (true) {
			Packet* recv_packet = ParsePacket(recv_buffer, bufferData_len);
			if (recv_packet == NULL) {//半包
				break;
			}
			bufferData_len -= GetPacketLen(recv_packet);
			memmove(recv_buffer, recv_buffer + GetPacketLen(recv_packet), bufferData_len);
			HandleCommand(recv_packet);//要在子线程里释放
		}
	}

	free(recv_buffer);
	recv_buffer = NULL;
	closesocket(g_client_socket);
	closesocket(g_server_socket);
	WSACleanup();
	return 0;
}

int InitAndListen_ServerSocket() {
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);        //1.初始化网络环境

	g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_socket == INVALID_SOCKET) {
		printf("服务器创建socket失败\r\n");      //2.创建服务端socket(AF_INET:IPv4,SOCK_STREAM:TCP协议,SOCK_DGRAM:UDP协议)
		return -1;
	}

	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9999);
	server_addr.sin_addr.S_un.S_addr = inet_addr("10.8.143.58");
	if (bind(g_server_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
		printf("服务器socket绑定IP地址失败\r\n");//3.为服务器socket绑定IP地址
		return -2;
	}

	if (listen(g_server_socket, 1) == SOCKET_ERROR) {
		printf("服务器启动监听失败\r\n");        //4.启动服务器socket监听(第二个参数backlog:允许完成三次握手的客户端数量)
		return -3;
	}

	return 0;
}

Packet* PackPacket(int cmdID, int body_len, char* buffer) {
	if (body_len < 0) {
		printf("数据包长度异常\r\n");
		return NULL;
	}
	if (body_len > 0 && buffer == NULL) {
		printf("数据包数据指针为空\r\n");
		return NULL;
	}

	Packet* packet = (Packet*)malloc(sizeof(PacketHeader) + body_len);
	if (packet == NULL) {
		printf("数据包内存分配失败\r\n");
		return NULL;
	}
	memset(packet, 0, sizeof(PacketHeader) + body_len);
	packet->header.magic = 0x55AA77CC;
	packet->header.cmdID = cmdID;
	packet->header.body_len = body_len;
	if (body_len > 0) {
		memcpy(packet->body, buffer, body_len);
	}

	return packet;
}

Packet* ParsePacket(char* buffer, int len) {
	if (buffer == NULL || len < sizeof(PacketHeader)) {//接收缓冲区为空或者接收缓冲区数据不够包头长度时返回null
		return NULL;
	}

	PacketHeader pckHeader;
	int index = 0;
	while (index <= len - 4) {                         //找魔数(魔数不一定出现在buffer首位)(index读到缓冲区倒数第四位没找到魔数时退出循环)，index记录读取魔数后光标位置 
		if (*(int*)(buffer + index) == 0x55AA77CC) {
			pckHeader.magic = *(int*)(buffer + index);
			index += 4;
			break;
		}
		index++;
	}

	if (index > len - 8) {                             //找到魔数后不足以读出cmdID+body_len时返回null(半包)
		return NULL;
	}

	pckHeader.cmdID = *(int*)(buffer + index);
	index += 4;
	pckHeader.body_len = *(int*)(buffer + index);
	index += 4;

	if (index + pckHeader.body_len > len)              //读出包头后不足以读出body[]时返回null(半包)
	{
		return NULL;
	}

	Packet* pck;                                       //空包(仅含包头)
	if (pckHeader.body_len == 0) {
		pck = (Packet*)malloc(sizeof(PacketHeader));
		memcpy(&pck->header, &pckHeader, sizeof(PacketHeader));
		return pck;
	}
	else if (pckHeader.body_len > 0) {
		pck = (Packet*)malloc(sizeof(PacketHeader) + pckHeader.body_len);
		memcpy(&pck->header, &pckHeader, sizeof(PacketHeader));
		memcpy(pck->body, buffer + index, pckHeader.body_len);
		return pck;
	}
	return NULL;
}

int GetPacketLen(Packet* pck) {
	if (pck != NULL) {
		return sizeof(PacketHeader) + pck->header.body_len;
	}
	return 0;
}

DWORD WINAPI HandleScreenThreadFuc(LPVOID lpThreadParameter) {
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		if (msg.message == WM_HANDLE_SCREEN) {
			Packet* packet = (Packet*)msg.lParam;
			HandleScreen(packet);
			free(packet);
		}
	}
	return 0;
}

DWORD WINAPI HandleMouseThreadFuc(LPVOID lpThreadParameter) {
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		if (msg.message == WM_HANDLE_MOUSE) {
			Packet* packet = (Packet*)msg.lParam;
			HandleMouse(packet);
			free(packet);
		}
	}
	return 0;
}

DWORD WINAPI HandleKeyBoardThreadFuc(LPVOID lpThreadParameter) {
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		if (msg.message == WM_HANDLE_KEYBOARD) {
			Packet* packet = (Packet*)msg.lParam;
			HandleKeyboard(packet);
			free(packet);
		}
	}
	return 0;
}

void HandleCommand(Packet* pck) {
	switch (pck->header.cmdID) {
	case CMD_SCREEN:
		PostThreadMessage(g_screen_thread_id, WM_HANDLE_SCREEN, NULL, (LPARAM)pck);
		break;
	case CMD_MOUSE:
		PostThreadMessage(g_mouse_thread_id, WM_HANDLE_MOUSE, NULL, (LPARAM)pck);
		break;
	case CMD_KEYBOARD:
		PostThreadMessage(g_keyboard_thread_id, WM_HANDLE_KEYBOARD, NULL, (LPARAM)pck);
		break;
	default:
		break;
	}
}

int HandleScreen(Packet* pck) {                       
	CImage image;                                     //GDI+图片对象,用来存储截取的屏幕画面
	HDC hScreen = NULL;                               //屏幕绘图句柄,相当于"屏幕的画板"
	HGLOBAL hMen = NULL;                              //Windows专用内存块,存储PNG图片数据
	IStream* pStream = NULL;                          //流对象,GDI+图片专用的数据容器
	char* pdata = NULL;                               //图片二进制数据指针                      

	hScreen = GetDC(NULL);	                          //获取整个屏幕的绘图设备句柄,NULL代表整个显示器
	if (hScreen == NULL) {
		printf("获取屏幕DC失败\r\n");
		return -1;
	}

	int bitWidth = GetDeviceCaps(hScreen, BITSPIXEL); //获取屏幕色深(每个像素的位数，决定图片清晰度)
	int sWidth = GetSystemMetrics(SM_CXSCREEN);       //获取屏幕宽
	int sHeight = GetSystemMetrics(SM_CYSCREEN);      //获取屏幕高

	image.Create(sWidth, sHeight, bitWidth);          //创建一个和屏幕一样大的空图片

	BitBlt(image.GetDC(), 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);//截图
	ReleaseDC(NULL, hScreen);	                      //释放屏幕画板(必须释放！否则造成系统资源泄漏)
	image.ReleaseDC();                                //释放截图时用到的绘图句柄image.GetDC()

	hMen = GlobalAlloc(GMEM_MOVEABLE, 0);             //分配一块内存存储截图二进制数据
	if (hMen == NULL) {
		image.Destroy();
		return -2;
	}

	HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);//创建IStream流对象，把上面的内存块包装成GDI+能识别的数据容器，true=用完流后，自动释放上面的hMen内存
	if (ret != S_OK) {
		GlobalFree(hMen);
		image.Destroy();
		return -3;
	}

	image.Save(pStream, ::Gdiplus::ImageFormatPNG);   //把图片转换成PNG格式二进制数据

	LARGE_INTEGER lg = { 0 };
	pStream->Seek(lg, STREAM_SEEK_SET, NULL);         //刚写完数据指针在流末尾，必须挪回开头才能读数据

	pdata = (char*)GlobalLock(hMen);                  //锁定内存，获取图片数据的指针
	int len = GlobalSize(hMen);	                      //获取PNG图片数据的长度

	Packet* packet = PackPacket(CMD_SCREEN, len, pdata);
	if (packet) {
		int send_len = send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
		if (send_len < 0 || send_len == 0) {
			printf("发送失败:%d\r\n", WSAGetLastError());
		}
		free(packet);
	}

	GlobalUnlock(hMen);                               //配对GlobalLock
	pStream->Release();                               //释放流对象,hMen自动释放
	image.Destroy();                                  //释放GDI+图片对象，释放系统绘图资源

	return 0;
}

int HandleMouse(Packet* pck) {
	if (pck->header.body_len != sizeof(Mouse)) {
		printf("鼠标数据包长度错误\r\n");
		return -1;
	}

	Mouse mouse;
	memcpy(&mouse, pck->body, sizeof(Mouse));

	SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);//移动鼠标到指定位置

	switch (mouse.action) {
	case MOUSE_MOVE:
		break;

	case MOUSE_LDOWN:
		mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
		break;
	case MOUSE_LUP:
		mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		break;

	case MOUSE_RDOWN:
		mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
		break;
	case MOUSE_RUP:
		mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		break;

	case MOUSE_MDOWN:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
		break;
	case MOUSE_MUP:
		mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
		break;

	//单击
	case MOUSE_LCLICK:
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		break;
	case MOUSE_RCLICK:
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		break;
	case MOUSE_MCLICK:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
		break;

	//双击
	case MOUSE_LDCLICK:
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		Sleep(50);
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
		break;
	case MOUSE_RDCLICK:
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		Sleep(50);
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
		break;
	case MOUSE_MDCLICK:
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
		Sleep(50);
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
		break;

    //滚轮滚动
	case MOUSE_WHEEL_UP:
		mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 120, GetMessageExtraInfo());
		break;
	case MOUSE_WHEEL_DOWN:
		mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120, GetMessageExtraInfo());
		break;
	default:
		printf("未知鼠标操作: %d\r\n", mouse.action);
		break;
	}

	return 0;
}

int HandleKeyboard(Packet* pck) {
	if (pck->header.body_len != sizeof(KeyBoard)) {
		printf("键盘数据包长度错误\r\n");
		return -1;
	}

	KeyBoard key_board;
	memcpy(&key_board, pck->body, sizeof(KeyBoard));

	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;		
	input.ki.wVk = key_board.virtual_code;
	input.ki.wScan = 0;
	input.ki.time = 0;
	input.ki.dwExtraInfo = 0;                                   //初始化Windows模拟输入结构体

	if (key_board.key_status == 0) {                            //0代表按键按下
		input.ki.dwFlags = 0;
	}
	else {                                                      //1代表按键抬起 
		input.ki.dwFlags = KEYEVENTF_KEYUP;
	}

	SendInput(1, &input, sizeof(INPUT));                        //执行按键模拟操作
	return 0;
}