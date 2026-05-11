#include <stdio.h>
#include <Windows.h>
#include <atlimage.h>
#pragma comment(lib,"ws2_32.lib")
//在InitAndConnect_ClientSocket中更改10.8.143.58为自己服务器IPv4地址
#define RECV_BUFFER_LEN 1024*1024*20

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

CRITICAL_SECTION g_cri_sec;                                     //线程同步锁
HWND g_hwnd = NULL;                                             //窗口句柄
SOCKET g_client_socket;                                         //客户端socket
SOCKADDR_IN g_server_addr;                                      //目标服务器IP+端口
CImage g_image;                                                 //GDI+图片对象
int g_remote_width = -1;                                        //远程屏幕宽
int g_remote_height = -1;                                       //远程屏幕高
int InitWindow(HINSTANCE hInstance, int nCmdShow);              //初始化并创建程序主窗口
int InitAndConnect_ClientSocket();                              //初始化客户端socket并向服务器发起连接请求
Packet* PackPacket(int cmdID, int body_len, char* buffer);	    //打包数据
Packet* ParsePacket(char* buffer, int len);					    //从tcp流读出一个完整数据包
int GetPacketLen(Packet* pck);								    //获取数据包长度
LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);//主线程处理函数:捕获客户在客户区的所有鼠标 键盘消息
DWORD WINAPI SendScreenCallBack(LPVOID lpThreadParameter);      //屏幕线程处理函数:向服务端请求屏幕画面→接收服务端发回的截图数据→把画面交给窗口渲染显示

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPreventInstance, PSTR pCmdLine, int nCmdShow) {
	InitializeCriticalSection(&g_cri_sec);               
	InitWindow(hInstance, nCmdShow);                      
	InitAndConnect_ClientSocket();                              

	unsigned long send_screen_thread_id = 0;                   
	CreateThread(NULL, 0, SendScreenCallBack, NULL, 0, &send_screen_thread_id);

	MSG msg = { 0 };
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

int InitWindow(HINSTANCE hInstance, int nCmdShow) {
	WNDCLASS ws = { 0 };
	LPCSTR CLASS_NAME = "MainWindow";

	ws.lpfnWndProc = winProc;
	ws.hInstance = hInstance;
	ws.lpszClassName = CLASS_NAME;
	ws.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	ws.hCursor = LoadCursorA(NULL, IDC_ARROW);
	ws.hIcon = LoadIconA(NULL, IDI_APPLICATION);
	ws.style = CS_HREDRAW | CS_VREDRAW;

	if (!RegisterClass(&ws)) {
		MessageBox(NULL, "窗口注册失败", "错误", MB_OK | MB_ICONERROR);
		return -1;
	}

	g_hwnd = CreateWindow(CLASS_NAME, "远程控制", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);
	if (g_hwnd == NULL) {
		MessageBox(NULL, "窗口创建失败", "错误", MB_OK | MB_ICONERROR);
		return -2;
	}

	ShowWindow(g_hwnd, nCmdShow);
	UpdateWindow(g_hwnd);
	return 0;
}

int InitAndConnect_ClientSocket() {
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);        //1.初始化网络环境
	
	g_client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (g_client_socket == INVALID_SOCKET) {
		printf("客户端创建socket失败\r\n");      //2.创建客户端socket
		return -1;
	}
	
	g_server_addr.sin_family = AF_INET;
	g_server_addr.sin_port = htons(9999);        //3.配置(要连接的服务器)的地址
	g_server_addr.sin_addr.S_un.S_addr = inet_addr("10.8.143.58");
	
	if (connect(g_client_socket, (sockaddr*)&g_server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
		printf("连接服务器失败\r\n");            //4.向服务器发送连接请求
		return -2;
	}
	printf("连接服务器成功\r\n");

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

LRESULT CALLBACK winProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static ULONGLONG mouse_tick = GetTickCount64();             //记录上次鼠标信息发包时间
	const int MOUSE_SEND_DELAY = 20;                            //用于鼠标信息节流,20ms发一次

	switch (msg) {
	case WM_PAINT:                                              //窗口绘制消息，由InvalidateRect(g_hwnd, NULL, FALSE)和UpdateWindow(g_hwnd)触发
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);                        //获取窗口的绘图句柄HDC
		if (!g_image.IsNull()) {
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);                  //获取窗口的客户区部分
			int client_width = client_rect.right - client_rect.left;//客户区宽
			int client_height = client_rect.bottom - client_rect.top;//客户区高

			int oldMode = SetStretchBltMode(hdc, HALFTONE);     //设置客户端系统的图片拉伸模式为HALFTONE(高清无损拉伸)并保存旧的绘图模式，最后要恢复
			SetBrushOrgEx(hdc, 0, 0, NULL);                     //配合HALFTONE模式使用，消除拉伸后的画面锯齿

			EnterCriticalSection(&g_cri_sec);                   //线程同步锁上锁
			int remote_width = g_image.GetWidth();              //远程屏幕宽
			int remote_height = g_image.GetHeight();            //远程屏幕高
			g_image.StretchBlt(hdc, 0, 0, client_width, client_height, 0, 0, remote_width, remote_height, SRCCOPY);//将远程截图拉伸绘制到客户区
			LeaveCriticalSection(&g_cri_sec);                   //线程同步锁解锁

			SetStretchBltMode(hdc, oldMode);                    //还原客户端系统原来的绘图模式
		}
		EndPaint(hwnd, &ps);                                    //释放BeginPaint
		return 0; 
	}

	case WM_MOUSEMOVE:                                          //鼠标移动
	{
		if (GetTickCount64() - mouse_tick < MOUSE_SEND_DELAY) { //节流:间隔20ms发一次
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
		mouse_tick = GetTickCount64();

		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);                              //鼠标在客户区相对左上角位置

		RECT client_rect;
		GetClientRect(hwnd, &client_rect);                      //获取窗口的客户区部分
		int client_width = client_rect.right - client_rect.left;//客户区宽
		int client_height = client_rect.bottom - client_rect.top;//客户区高

		if (g_remote_width != -1 && g_remote_height != -1) {
			int rxPos = xPos * g_remote_width / client_width;
			int ryPos = yPos * g_remote_height / client_height; //映射到远程屏幕的坐标

			Mouse mouse;
			mouse.action = MOUSE_MOVE;
			mouse.ptXY.x = rxPos;
			mouse.ptXY.y = ryPos;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_LBUTTONDOWN:                                        //鼠标左键按下
	{
		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.action = MOUSE_LDOWN;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_LBUTTONUP:                                          //鼠标左键抬起 
	{
		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.action = MOUSE_LUP;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_LBUTTONDBLCLK:                                      //鼠标左键双击 
	{
		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.action = MOUSE_LDCLICK;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_RBUTTONDOWN:                                        //鼠标右键按下
	{
		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.action = MOUSE_RDOWN;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_RBUTTONUP:                                          //鼠标右键抬起 
	{
		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.action = MOUSE_RUP;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_MOUSEWHEEL:                                         //鼠标滚轮滚动 
	{
		short delta = GET_WHEEL_DELTA_WPARAM(wParam);//获取滚轮滚动方向 正值=向上滚 负值=向下滚

		int xPos = LOWORD(lParam);
		int yPos = HIWORD(lParam);
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int w = client_rect.right - client_rect.left;
		int h = client_rect.bottom - client_rect.top;

		if (g_remote_width != -1) {
			int rx = xPos * g_remote_width / w;
			int ry = yPos * g_remote_height / h;

			Mouse mouse;
			mouse.ptXY.x = rx;
			mouse.ptXY.y = ry;

			if (delta > 0) {
				mouse.action = MOUSE_WHEEL_UP;//向上滚
			}
			else {
				mouse.action = MOUSE_WHEEL_DOWN;//向下滚
			}

			Packet* packet = PackPacket(CMD_MOUSE, sizeof(Mouse), (char*)&mouse);
			if (packet) {
				send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
				free(packet);
			}
		}
		return 0;
	}

	case WM_KEYDOWN:                                            //普通按键按下
	case WM_SYSKEYDOWN:                                         //系统按键按下
	{
		KeyBoard key_board;
		key_board.virtual_code = wParam;//按键虚拟码
		key_board.key_status = 0;       //按键状态

		Packet* packet = PackPacket(CMD_KEYBOARD, sizeof(KeyBoard), (char*)&key_board);
		if (packet) {
			send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
			free(packet);
		}
		return 0;
	}

	case WM_KEYUP:                                              //普通按键抬起
	case WM_SYSKEYUP:                                           //系统按键抬起
	{
		KeyBoard key_board;
		key_board.virtual_code = wParam;
		key_board.key_status = 1; 

		Packet* packet = PackPacket(CMD_KEYBOARD, sizeof(KeyBoard), (char*)&key_board);
		if (packet) {
			send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
			free(packet);
		}
		return 0;
	}
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

DWORD WINAPI SendScreenCallBack(LPVOID lpThreadParameter) {
	char* recv_buffer = (char*)malloc(RECV_BUFFER_LEN);
	if (recv_buffer == NULL) {
		return -1;
	}

	while (true) {
		Packet* packet = PackPacket(CMD_SCREEN, 0, NULL);
		if (packet == NULL) {
			continue;
		}

		int send_len = send(g_client_socket, (char*)packet, GetPacketLen(packet), 0);
		free(packet);

		int len = recv(g_client_socket, recv_buffer, RECV_BUFFER_LEN, 0);
		if (len <= 0) {
			printf("连接断开，接收截图线程退出\n");
			break;
		}
		Packet* recv_packet = ParsePacket(recv_buffer, len);
		if (recv_packet == NULL) {
			continue;
		}

		HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0);
		if (hMen == NULL) {                      //分配一块内存存储服务端发来的截图二进制数据
			free(recv_packet);
			continue;
		}

		IStream* pStream = NULL;                 //创建IStream流对象，把上面的内存块包装成GDI+能识别的数据容器，true=用完流后，自动释放上面的hMen内存
		HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);

		if (ret == S_OK) {
			ULONG lenght = 0;                    //流创建成功时把服务端发来的截图二进制数据写入流容器
			pStream->Write(recv_packet->body, recv_packet->header.body_len, &lenght);
			free(recv_packet);                   //recv_packet中的截图二进制数据已经写入流容器，需释放掉

			EnterCriticalSection(&g_cri_sec);    //线程同步锁上锁
			LARGE_INTEGER lg = { 0 };            //刚写完数据指针在流末尾，必须挪回开头才能读数据
			pStream->Seek(lg, STREAM_SEEK_SET, NULL);
			if (!g_image.IsNull()) {             //清空上一张截图
				g_image.Destroy();
			}
			g_image.Load(pStream);               //截图二进制数据还原为图片
			if (g_remote_width == -1 && g_remote_height == -1) {
				g_remote_width = g_image.GetWidth();
				g_remote_height = g_image.GetHeight();
			}
			LeaveCriticalSection(&g_cri_sec);    //线程同步锁解锁

			InvalidateRect(g_hwnd, NULL, FALSE); //提示系统重绘图片
			UpdateWindow(g_hwnd);                //重绘图片并显示

			pStream->Release();
			pStream = NULL;
		}
	}

	free(recv_buffer);
	recv_buffer = NULL;
	return 0;
}