/*
 ============================================================================
 Name        : control_lan.c
 Author      : Crazy Boy Feng
 Version     :
 Copyright   : GNU General Public License
 Description : Agent X One LAN controller in C, ANSI-style
 ============================================================================
 */
#include "agentx1.h"
int sock_lan; //连接
struct ifreq if_lan; //网络接口
void find_lan(char *interface) { //打开lan连接
	if ((sock_lan = socket(PF_PACKET, SOCK_RAW, htons(0x888e))) < 0) { //建立连接
		error("LAN socket() error"); //出错提示
	}
	strcpy(if_lan.ifr_name, interface); //指定lan接口
	printf("\tLAN interface: %s\n", if_lan.ifr_name);
	if (ioctl(sock_lan, SIOCGIFINDEX, &if_lan) < 0) { //查找接口
		error("LAN ioctl() error"); //出错提示
	}
	struct sockaddr_ll sll_lan;
	sll_lan.sll_family = AF_PACKET; //设置lan协议族
	sll_lan.sll_ifindex = if_lan.ifr_ifindex; //设置lan接口
	sll_lan.sll_protocol = htons(0x888e); //设置lan协议类型
	if (bind(sock_lan, (struct sockaddr *) &sll_lan, sizeof(sll_lan)) < 0) { //将接口与地址绑定
		error("LAN bind() error"); //出错提示
	}
}
void open_lan(void) {
	if(ioctl(sock_lan, SIOCGIFFLAGS, &if_lan)<0){//准备混杂模式
		error("LAN ioctl() error"); //出错提示
	}
	if(promiscuous!=0){//混杂模式
		if_lan.ifr_flags |= IFF_PROMISC;//混杂模式
	}else{//非混杂模式
		if_lan.ifr_flags &=~ IFF_PROMISC;//非混杂模式
	}
	if(ioctl(sock_lan, SIOCGIFFLAGS, &if_lan)<0){//以混杂模式打开网卡
		error("LAN ioctl() error"); //出错提示
	}
	if (ioctl(sock_lan, SIOCGIFHWADDR, &if_lan) < 0) { //查询mac
		error("LAN ioctl() error"); //出错提示
	}
	memcpy(mac_lan, if_lan.ifr_hwaddr.sa_data, 6); //获得mac地址
	printf("\tLAN MAC address: %02x-%02x-%02x-%02x-%02x-%02x\n", mac_lan[0],
			mac_lan[1], mac_lan[2], mac_lan[3], mac_lan[4], mac_lan[5]);
}
void filter_lan(unsigned char *buffer) { //锁定客户端
	if(promiscuous==0){//非混杂模式
		memcpy(server_wan,buffer,6);//初始化server为组播mac
	}
	memcpy(client_lan, buffer + 6, 6);	//取出client
	printf("\tClient MAC address: %02x-%02x-%02x-%02x-%02x-%02x\n",
			client_lan[0], client_lan[1], client_lan[2], client_lan[3],
			client_lan[4], client_lan[5]);
}
void send_lan(unsigned char *buffer, int length) { //lan发包
	memcpy(buffer, client_lan, 6);	//修改dst为client
	if(promiscuous==0){//非混杂模式
		memcpy(buffer + 6, mac_lan, 6);	//修改src为lan
	}
	if (sendto(sock_lan, buffer, length, 0, NULL, 0) < 0) {
		error("LAN sendto() error");	//错误提示
	}
}
void work_lan(void) { //lan线程
	puts("Opening the LAN socket connection...");
	open_lan(); //查询接口
	puts("Receiving the packets from LAN...");
	int len_lan; //包长度
	unsigned char buf_lan[1024]; //缓冲区
	while ((len_lan = recvfrom(sock_lan, buf_lan, 1024, 0, NULL, NULL )) > 0) { //循环接收
		if (state == X_PRE) { //准备状态
			switch (buf_lan[0x0f]) { //比较type
			case 0x01: //start包
				time_lan = 0; //初始时间标志
				repeat_lan = 0; //初始中继标志
				puts("Got a EAPOL Start packet from LAN!");
				puts("Refreshing the network interfaces...");
				refresh_wan(); //dhcp并输出
				puts("Reading the client MAC address...");
				filter_lan(buf_lan); //锁定客户端
				puts("Modifying the EAPOL Start packet...");
				set_head(buf_lan, len_lan); //修改加密位
				puts("Turning the work mode to Transmission...");
				state = X_ON; //切换转发模式
				puts("Sending the EAPOL Start packet to WAN...");
				send_wan(buf_lan, len_lan); //发start
				break;
			}
		} else if (state == X_ON&& memcmp(client_lan, buf_lan + 6, 6) == 0) { //转发状态且来自于指定客户端
			switch (buf_lan[0x0f]) {	//比较type
			case 0x01://start
				time_lan = 0; //初始时间标志
				repeat_lan = 0; //初始中继标志
				puts("Got a EAPOL Start packet from client!");
				puts("Reading the client MAC address...");
				filter_lan(buf_lan); //锁定客户端
				puts("Modifying the EAPOL Start packet...");
				set_head(buf_lan, len_lan); //修改加密位
				puts("Sending the EAPOL Start packet to WAN...");
				send_wan(buf_lan, len_lan); //发start
				break;
			case 0x02:	//logoff
				puts("Got a EAPOL Logoff packet from client!");
				puts("Modifying the EAPOL Logoff packet...");
				set_head(buf_lan, len_lan);	//修改logoff
				puts("Turning the work mode to Initialization...");
				state = X_PRE;	//准备模式
				puts("Sending the EAPOL Logoff packet to server...");
				send_wan(buf_lan, len_lan);	//发logoff
				break;
			case 0xbf:	//echo
				puts("Got a EAPOL Echo packet from client!");
				puts("Reading the interval argument...");
				get_interval(buf_lan);	//收集中继间隔
				puts("Reading the repeat parameters...");
				get_echo(buf_lan);	//收集中继参数
				puts("Sending the EAPOL Echo packet to server...");
				send_wan(buf_lan, len_lan);	//发送echo
				if (interval != 0&& repeat_lan == 1) {	//所有变量全部获得且正确
					puts("Storing the EAPOL Echo packet...");
					size_echo = len_lan;
					memcpy(data_echo, buf_lan, size_echo);	//复制数据
					puts("Modifying the EAP Success packet...");
					size_temp = set_success(data_temp, size_temp);	//修改提示
					puts("Turning the work mode to Animation...");
					state = X_OFF;	//等待（自动）模式
					puts("Sending the EAP Success packet to client...");
					send_lan(data_temp, size_temp);	//发送success（注意客户端会立即回应hello）
					alarm(interval);//启动中继定时器
				}else{
					puts("Resetting the invalid echo work...");
				}
				break;
			case 0x00:	//eap
				if (buf_lan[0x12] == 0x02 && buf_lan[0x16] == 0x04) {//response-md5
					puts(
							"Got a EAP Response MD5-Challenge packet from client!");
					puts("Modifying the EAP Response MD5-Challenge packet...");
					set_head(buf_lan, len_lan);	//修改md5
					puts(
							"Sending the EAP Response MD5-Challenge packet to server...");
					send_wan(buf_lan, len_lan);	//发送md5
					break;
				} else if (buf_lan[0x12] == 0x02 && buf_lan[0x16] == 0x01) {//response-id
					puts("Got a EAP Response Identity packet from client!");
					puts("Modifying the EAP Response Identity packet...");
					set_head(buf_lan, len_lan);	//修改id
					puts(
							"Sending the EAP Response Identity packet to server...");
					send_wan(buf_lan, len_lan);	//发送id
				}
				break;
			}	//switch
		} else if (state == X_OFF
				&& memcmp(client_lan, buf_lan + 6, 6) == 0) {
			switch (buf_lan[0x0f]) {	//比较type
			case 0x02:	//logoff
				puts("Got a EAPOL Logoff packet from client!");
				puts("Turning the work mode to Repetition...");
				state = X_RE;	//中继模式
				puts("Storing the EAPOL Logoff packet...");
				size_temp = len_lan;
				memcpy(data_temp, buf_lan, size_temp);	//复制数据
				break;
			/*case 0xbf:	//echo
				puts("Got a EAPOL Echo packet from client!");
				puts("Reading the interval argument...");
				get_interval(buf_lan);	//收集中继间隔
				puts("Reading the repeat parameters...");
				get_echo(buf_lan);	//修正key和count
				puts("Sending the EAPOL Echo packet to server...");
				send_wan(buf_lan, len_lan);	//发送echo
				alarm(interval);//启动定时器
				break;*///为了防止客户端应答提示success包从而导致echo过于频繁，off状态不再转发echo，直接中继
			}
		} else if (state == X_RE) {
			switch (buf_lan[0x0f]) { //比较type
			case 0x01: //start包
				time_lan = 0; //初始时间标志
				repeat_lan = 0; //初始中继标志
				puts("Got a EAPOL Start packet from LAN!");
				puts("Reading the client MAC address...");
				filter_lan(buf_lan); //取出client
				puts("Modifying the EAPOL Logoff packet...");
				set_head(data_temp, size_temp);	//修改加密位
				puts("Turning the work mode to Transmission...");
				state = X_ON;	//转发模式
				puts("Sending the EAPOL Logoff packet to server...");
				send_wan(data_temp, size_temp);	//发logoff
				puts("Refreshing the network interfaces...");
				refresh_wan(); //dhcp并输出
				puts("Sending the EAPOL Start packet to WAN...");
				send_wan(buf_lan, len_lan);	//发start
				break;
			}
		}	//if
	}	//while
}
