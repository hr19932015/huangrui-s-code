#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <resolv.h>
#include <signal.h>
#include <getopt.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define My_ETHER_ADDR_LEN 6
struct my_ether_addr {
	uint8_t addr_bytes[My_ETHER_ADDR_LEN];
}__attribute__((__packed__));

struct ether_hdr {
	struct my_ether_addr d_addr;
	struct my_ether_addr s_addr;
	uint16_t ether_type;
}__attribute__((__packed__));

#define HOPv1		0xC1
#define TYPE_DATA	0x01
#define TYPE_ACK	0x02
#define TYPE_BYSN	0x03
struct Hop_header
{
	struct ether_hdr ether_header;
	uint8_t hop_ver;	
	uint8_t type;	
	uint16_t block_sequence;
	uint16_t piece_sequence;
	uint16_t piece_size;
}__attribute__((__packed__));
typedef struct Hop_header Hop_header_t;

#define PHYSICALPORT_UP       		"eth3"
#define PHYSICALPORT_UP_MAC		"000003"

#define PHYSICALPORT_DOWN       	"eth4"
#define PHYSICALPORT_DOWN_MAC		"000004"

#define BUFSIZE			2048 	//This number must be bigger than 1500. 
#define PIECE_SIZE 		1024 	//The Unit is byte! It < (BUFSIZE - header's length).
#define PIECE_NUMBER		1000	//It means how many packets sent in a turn! 

uint8_t 	send_mbuf[PIECE_NUMBER][BUFSIZE]={0};	//These bufs store the packets which will be sent, including header and data!
uint16_t 	send_mbuf_piece_size[PIECE_NUMBER]={0};
uint16_t	send_mbuf_piece_number=0;

static int 	RecvBufSize=BUFSIZE;

uint8_t 	receive_mbuf[PIECE_NUMBER][BUFSIZE]={0};
uint16_t 	receive_mbuf_piece_size[PIECE_NUMBER]={0};
uint16_t	receive_mbuf_piece_number=0;


//chunk[chunk_send_index].mbuf_piece_number
//-------------------------------------------------------
#define EMPTY 	0
#define MODIFY 	1
#define FULL 	2
struct chunk_mbuf
{
	uint8_t		mbuf[PIECE_NUMBER][BUFSIZE];
	uint16_t 	mbuf_piece_size[PIECE_NUMBER];
	uint16_t	mbuf_piece_number;

	//0=empty,1=modify,2=full;
	uint8_t 	mbuf_state_flag;
};
typedef struct chunk_mbuf chunk_mbuf_t;

#define chunk_mbuf_num 50
chunk_mbuf_t chunk[chunk_mbuf_num];
//This number will not bigger than "chunk_mbuf_num".                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
uint8_t chunk_receive_index=0;
uint8_t chunk_send_index=0;


void clear_all_chunk_mbuf()
{
	memset(chunk ,0 , chunk_mbuf_num*sizeof(chunk_mbuf_t));
}

uint8_t locate_a_empty_mbuf()
{
	//This is a block function!!!!
	printf("[From %s]Locating a empty in chunk[%d]\n",__func__,chunk_receive_index);
	while(chunk[chunk_receive_index].mbuf_state_flag!=EMPTY);
	if(chunk[chunk_receive_index].mbuf_state_flag==EMPTY)
	{
		int temp=chunk_receive_index; 
		chunk_receive_index=(chunk_receive_index+1)%chunk_mbuf_num;
		printf("[From %s]successsfully\n",__func__);
		return temp;
	}else
	{
		//-1 means there is no empty mbuf!
		return -1;
	}
}

uint8_t locate_a_full_mbuf()
{
	//This is a block function!!!!
	printf("[From %s]Locating a full in chunk[%d]\n",__func__,chunk_send_index);
	while(chunk[chunk_send_index].mbuf_state_flag!=FULL);
	if(chunk[chunk_send_index].mbuf_state_flag==FULL)
	{
		printf("[From %s]successsfully\n",__func__);
		return chunk_send_index;
		//The send buf will be used many times!
		//chunk_send_index=(chunk_send_index++)%chunk_mbuf_num;
	}else
	{
		//-1 means there is no full mbuf!
		return -1;
	}
}


//-------------------------------------------------------





uint8_t		receive_bitmap[PIECE_NUMBER]={0};

#define port_name_length 9
struct PORT
{
	char name[port_name_length];
	struct my_ether_addr addr;
	int sockfd_send;
	int sockfd_receive;
	struct sockaddr Port;
};
typedef struct PORT port_t;

port_t port_up;
port_t port_down;


//There is a bug! But it does not work!
//In the bysn,it is not used at all!
uint16_t blockcounter=0;

//线程函数

void *thread_recv();

int send_ok=1;
int move_ok=0;

uint8_t send_bysn_waiting_flag=0;
void *thread_send_down();
void *thread_receive_down();
void *bysn_resend();

//This is a thread to print the log information~
void *thread_printlog();

/*****************************************
* 功能描述：物理网卡混杂模式属性操作
*****************************************/
static int Ethernet_SetPromisc(const char *pcIfName,int fd,int iFlags)
{
	int iRet = -1;
	struct ifreq stIfr;
	//获取接口属性标志位
	strcpy(stIfr.ifr_name,pcIfName);
	iRet = ioctl(fd,SIOCGIFFLAGS,&stIfr);
	if(0 > iRet){
		perror("[Error]Get Interface Flags");   
		return -1;
	}
	if(0 == iFlags){
		//取消混杂模式
		stIfr.ifr_flags &= ~IFF_PROMISC;
	}
	if(iFlags>0){
		//设置为混杂模式
		stIfr.ifr_flags |= IFF_PROMISC;
	}
	//设置接口标志
	iRet = ioctl(fd,SIOCSIFFLAGS,&stIfr);
	if(0 > iRet){
		perror("[Error]Set Interface Flags");
		return -1;
	}
	return 0;
}

/*****************************************
* 函数名称：Ethernet_InitSocket
*****************************************/
static int Ethernet_InitSocket(char Physical_Port[port_name_length])
{
	int iRet = -1;
	int fd = -1;
	struct ifreq stIf;
	struct sockaddr_ll stLocal = {0};
	//创建SOCKET
	fd = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
	if (0 > fd){
		perror("[Error]Initinate L2 raw socket");
		return -1;
	}
	//网卡混杂模式设置
	Ethernet_SetPromisc(Physical_Port,fd,1);
	//设置SOCKET选项
	iRet = setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&RecvBufSize,sizeof(int));
	if (0 > iRet){
		perror("[Error]Set socket option");
		close(fd);
	}
	//获取物理网卡接口索引
	strcpy(stIf.ifr_name,Physical_Port);
	iRet = ioctl(fd,SIOCGIFINDEX,&stIf);
	if (0 > iRet){
		perror("[Error]Ioctl operation");
		close(fd);
		return -1;
	}
	//绑定物理网卡
	stLocal.sll_family = PF_PACKET;
	stLocal.sll_ifindex = stIf.ifr_ifindex;
	stLocal.sll_protocol = htons(ETH_P_ALL);
	iRet = bind(fd,(struct sockaddr *)&stLocal,sizeof(stLocal));
	if (0 > iRet){
		perror("[Error]Bind the interface");
		close(fd);
		return -1;
	}
	return fd;   
}

int main(int argc,char *argv[])
{	
	int i=0;
	clear_all_chunk_mbuf();
	for(i=0;i<chunk_mbuf_num;i++)
	{
		printf("[%s]chunk[%d].mbuf_state_flag=%d\n",__func__,i,chunk[i].mbuf_state_flag);
	}

//---------------------------------------------------------------------
	memcpy(port_up.name,PHYSICALPORT_UP,port_name_length);
	memcpy(port_up.addr.addr_bytes,PHYSICALPORT_UP_MAC,My_ETHER_ADDR_LEN);
	port_up.sockfd_receive=Ethernet_InitSocket(port_up.name);
	if((port_up.sockfd_send=socket(PF_PACKET,SOCK_PACKET,htons(ETH_P_ALL)))==-1)
	{
		printf("Socket Error\n");
		exit(0);
	}
	memset(&port_up.Port,0,sizeof(port_up.Port));
	strcpy(port_up.Port.sa_data,port_up.name);
//---------------------------------------------------------------------
	pthread_t pthread_recv;
	if(pthread_create(&pthread_recv, NULL, thread_recv, (void *)&port_up)!=0)
	{
		perror("Creation of receive thread failed.");
	}
	

	
//---------------------------------------------------------------------
	memcpy(port_down.name,PHYSICALPORT_DOWN,port_name_length);
	memcpy(port_down.addr.addr_bytes,PHYSICALPORT_DOWN_MAC,My_ETHER_ADDR_LEN);
	port_down.sockfd_receive=Ethernet_InitSocket(port_down.name);
	if((port_down.sockfd_send=socket(PF_PACKET,SOCK_PACKET,htons(ETH_P_ALL)))==-1)
	{
		printf("Socket Error\n");
		exit(0);
	}
	memset(&port_down.Port,0,sizeof(port_down.Port));
	strcpy(port_down.Port.sa_data,port_down.name);
//---------------------------------------------------------------------
	pthread_t pthread_send_down;
	if(pthread_create(&pthread_send_down, NULL, thread_send_down, (void *)&port_down)!=0)
	{
		perror("Creation of send thread failed.");
	}
	pthread_t pthread_receive_down;
	if(pthread_create(&pthread_receive_down, NULL, thread_receive_down, (void *)&port_down)!=0)
	{
		perror("Creation of send thread failed.");
	}
	pthread_t pthread_bysn_resend;
	if(pthread_create(&pthread_bysn_resend, NULL,bysn_resend,(void *)&port_down)!=0)
	{
		perror("Creation of receive thread failed.");
	}
	
//----------------------------------------------------------------------------------------
	pthread_t pthread_printlog;
	if(pthread_create(&pthread_printlog, NULL,thread_printlog,NULL)!=0)
	{
		perror("Creation of receive thread failed.");
	}
	
	while(1)
	{
		
	}
	
	exit(0);
	return (EXIT_SUCCESS);
}


void load_one_block_to_send_buf(FILE *fp)
{

}

/*
struct chunk_mbuf
{
	uint8_t		mbuf[PIECE_NUMBER][BUFSIZE];
	uint16_t 	mbuf_piece_size[PIECE_NUMBER];
	uint16_t	mbuf_piece_number;

	//0=empty,1=modify,2=full;
	uint8_t 	mbuf_state_flag;
};
*/

void move_recive_buf_to_send_buf()
{
	//It will be blocked!
	uint8_t receive_index=locate_a_empty_mbuf();

	chunk[receive_index].mbuf_state_flag=MODIFY;
	memcpy(chunk[receive_index].mbuf,receive_mbuf,PIECE_NUMBER*BUFSIZE);
	memcpy(chunk[receive_index].mbuf_piece_size,receive_mbuf_piece_size,PIECE_NUMBER*2);
	chunk[receive_index].mbuf_piece_number=receive_mbuf_piece_number;
	chunk[receive_index].mbuf_state_flag=FULL;
	//To change the source mac address!
	int i;
	for(i=0;i<chunk[receive_index].mbuf_piece_number;i++)
	{
		Hop_header_t * hop_header =(Hop_header_t *)chunk[receive_index].mbuf[i];
		memcpy(hop_header->ether_header.s_addr.addr_bytes,port_down.addr.addr_bytes,sizeof(struct my_ether_addr));
	}
	
	printf("[%s]chunk[%d]Move OK~!\n",__func__,receive_index);
	//getchar();
}

void write_one_block_to_receive_file(FILE *fp)
{
	int i;
	printf("[%s]receive_mbuf_piece_number=%d\n",__func__,receive_mbuf_piece_number);
	for(i=0;i<receive_mbuf_piece_number;i++)
	{
		fwrite(receive_mbuf[i]+sizeof(Hop_header_t),1,receive_mbuf_piece_size[i],fp);
		printf("receive_mbuf[%d]hop_header->piece_size=%d\n",i,receive_mbuf_piece_size[i]);
	}
	printf("[%s]Write OK~!\n",__func__);
}

#define send_dealy_usleep 1
void flush_send_buf(port_t * port){
	//This is a block function to wait a full mubuf!!
	uint8_t send_index=locate_a_full_mbuf();
	int piececounter=0;
	for(piececounter=0; piececounter<chunk[send_index].mbuf_piece_number; piececounter++)
	{
		sendto(port->sockfd_send,chunk[send_index].mbuf[piececounter],chunk[send_index].mbuf_piece_size[piececounter]+sizeof(struct Hop_header),0,&port->Port,sizeof(port->Port));
		Hop_header_t * hop_header =(Hop_header_t *)chunk[send_index].mbuf[piececounter];
		printf("[ %s]Send < %d : %d >\n",__func__,hop_header->block_sequence,hop_header->piece_sequence);	
		usleep(send_dealy_usleep );
	}
}

void re_flush_send_buf(port_t * port,uint8_t receive_bitmap[PIECE_NUMBER]){
	uint8_t send_index=locate_a_full_mbuf();	
	int piececounter=0;
	for(piececounter=0; piececounter<chunk[send_index].mbuf_piece_number; piececounter++)
	{
		if(receive_bitmap[piececounter]==0)
		{
			sendto(port->sockfd_send,chunk[send_index].mbuf[piececounter],chunk[send_index].mbuf_piece_size[piececounter]+sizeof(struct Hop_header),0,&port->Port,sizeof(port->Port));
			Hop_header_t * hop_header =(Hop_header_t *)chunk[send_index].mbuf[piececounter];
			printf("[ %s]Send < %d : %d >\n",__func__,hop_header->block_sequence,hop_header->piece_sequence);		
		}
		usleep(send_dealy_usleep);
	}
}

uint64_t bysn_serial_number=0;
struct bysn_content{
	uint16_t blockcounter;
	uint16_t piececounter;
	uint64_t serial_number;
}__attribute__((__packed__));
typedef struct bysn_content bysn_content_t;

void send_bysn(port_t * port)
{
	if(chunk[chunk_send_index].mbuf_piece_number==0)
	{
		printf("[ %s]END of bysn!\n",__func__);
	}	
	
	uint8_t bysn_mbuf[BUFSIZE]={0};
	Hop_header_t * hop_header =(Hop_header_t *)bysn_mbuf;
	memset(&hop_header->ether_header.d_addr,0xF,sizeof(struct my_ether_addr));
	memcpy(&hop_header->ether_header.s_addr,port->addr.addr_bytes,sizeof(struct my_ether_addr));
	hop_header->ether_header.ether_type=0x0008;
	hop_header->hop_ver=HOPv1;
	hop_header->type=TYPE_BYSN;
	hop_header->piece_sequence=0;
	hop_header->block_sequence=0;
	
	bysn_content_t * bysn_content=(bysn_content_t *)(bysn_mbuf+sizeof(Hop_header_t));
	//bysn_content->blockcounter=blockcounter;
	bysn_content->piececounter=chunk[chunk_send_index].mbuf_piece_number;
	bysn_content->serial_number=bysn_serial_number++;
	sendto(port->sockfd_send,bysn_mbuf,sizeof(Hop_header_t)+sizeof(bysn_content_t),0,&port->Port,sizeof(port->Port));
	printf("[ %s]Send the Bysn!\n",__func__);
}

void *bysn_resend(void *port_point)
{
	port_t * port=(port_t*)port_point;
	while(1)
	{
		while(send_bysn_waiting_flag==0);
		usleep(1000000);
		if(send_bysn_waiting_flag==1)
		{                                 
			printf("[ %s]The Last Bysn is not successful! Resend it!\n",__func__);
			send_bysn(port);
		}
	}
}

struct ack_content{
	uint8_t receive_bitmap[PIECE_NUMBER];
	uint64_t serial_number_of_bysn;
}__attribute__((__packed__));
typedef struct ack_content ack_content_t;

void send_ack(port_t * port,uint64_t serial_number)
{
	uint8_t ack_mbuf[BUFSIZE]={0};
	Hop_header_t * hop_header =(Hop_header_t *)ack_mbuf;
	memset(&hop_header->ether_header.d_addr,0xF,sizeof(struct my_ether_addr));
	memcpy(&hop_header->ether_header.s_addr,port->addr.addr_bytes,sizeof(struct my_ether_addr));
	hop_header->ether_header.ether_type=0x0008;
	hop_header->hop_ver=HOPv1;
	hop_header->type=TYPE_ACK;
	hop_header->piece_sequence=0;
	hop_header->block_sequence=0;
	
	//Send all the bitmap to the other hand!
	memcpy(ack_mbuf+sizeof(Hop_header_t),receive_bitmap,PIECE_NUMBER);
	ack_content_t * ack_content=(ack_content_t *)(ack_mbuf+sizeof(Hop_header_t));
	ack_content->serial_number_of_bysn=serial_number;
	sendto(port->sockfd_send,ack_mbuf,sizeof(Hop_header_t)+sizeof(ack_content_t),0,&port->Port,sizeof(port->Port));
	printf("[ %s]Send the ACK!\n",__func__);
}

void *thread_send_down(void * port_point)
{

	port_t * port=(port_t*)port_point;
	
	while(1)
	{
		while(send_ok==0);
		send_ok=0;
		flush_send_buf(port);
		send_bysn(port);
		send_bysn_waiting_flag=1;
	}
	pthread_exit(NULL);
}

uint16_t current_block_sequence=-1;
void process_DATA(Hop_header_t * hop_header)
{
    	if(current_block_sequence != hop_header->block_sequence)
	{
		printf("[%s]Receiving a new block!Clear the bitmap information!\n",__func__);
		if(hop_header->block_sequence > current_block_sequence&&current_block_sequence>=0)
		{
                        //When the forwarder receive the a biger block_sequence, the action of the moving will be excute!
                        //But when there are no DATA packet coming, tht action of the moving will be tiggered by the 'the_last_block_flag' from BYSN!
			printf("[%s]A new Block!! Please move the buf!\n",__func__);
			//getchar();

			//while(send_ok==0);
			move_ok=0;
			move_recive_buf_to_send_buf();
			move_ok=1;
		}
		current_block_sequence=hop_header->block_sequence;
		memset(receive_bitmap,0,PIECE_NUMBER);
		receive_mbuf_piece_number=0;
		//Clear the information!
		
		
	}
	printf("[ %s] < %d : %d > receive_mbuf_piece_number=%d\n",__func__,hop_header->block_sequence,hop_header->piece_sequence,receive_mbuf_piece_number);
	memcpy(receive_mbuf[hop_header->piece_sequence],hop_header,BUFSIZE);
	receive_mbuf_piece_size[hop_header->piece_sequence]=hop_header->piece_size;
	if(receive_bitmap[hop_header->piece_sequence]==0)
	{
		receive_bitmap[hop_header->piece_sequence]=1;
		receive_mbuf_piece_number++;
	}
}

int move_empty_time=0;
void process_BYSN(Hop_header_t * hop_header,port_t * port)
{
	//getchar();
	int i;
	bysn_content_t * bysn_content=(bysn_content_t *)(hop_header+1);
	printf("[ %s]Need to check %d pieces\n",__func__,bysn_content->piececounter);
	
	if(bysn_content->piececounter==0 &&move_empty_time==0)
	{
		printf("[ %s]***!!!***The file has been received totally!\n",__func__);
		//There is also a last block file to be writed!
		//while(send_ok==0);
		move_ok=0;
		move_recive_buf_to_send_buf();
		move_ok=1;
		memset(receive_bitmap,0,PIECE_NUMBER);
		receive_mbuf_piece_number=0;		
		
		//To put an empty block to tell the reciver that the file has been received!
		move_ok=0;
		move_recive_buf_to_send_buf();
		move_ok=1;
		move_empty_time++;
	}
	
	for(i=0;i<bysn_content->piececounter;i++)
	{
		if(receive_bitmap[i]==0){
			printf("Lost %d\n",i);
		}
	}
	//Because the sender's serial_number has been add one! 
	send_ack(port,bysn_content->serial_number+1);
}
void process_ACK(Hop_header_t * hop_header, port_t * port)
{
	ack_content_t * ack_content=(ack_content_t *)(hop_header+1);
	
	if(ack_content->serial_number_of_bysn!=bysn_serial_number)
	{
		printf("[ %s]The ACK is too old!\n",__func__);	
		return;
	}
	int i;
	send_bysn_waiting_flag=0;
	printf("[%s ]chunk[%d].mbuf_piece_number=%d\n",__func__,chunk_send_index,chunk[chunk_send_index].mbuf_piece_number);
	
	int re_flush_send_buf_flag=0;
	for(i=0;i<chunk[chunk_send_index].mbuf_piece_number;i++)
	{
		//printf("ack_content->receive_bitmap[%d]=%d\n",i,ack_content->receive_bitmap[i]);
		if(ack_content->receive_bitmap[i]==0)
		{
			re_flush_send_buf_flag=1;
		}
	}
	
	if(re_flush_send_buf_flag==1)
	{
		re_flush_send_buf(port,ack_content->receive_bitmap);
		send_bysn(port);
		send_bysn_waiting_flag=1;
	}else{
		printf("[ %s]Please send the next block!\n",__func__);
		send_ok=1;
		move_ok=0;
		
		uint8_t send_index=locate_a_full_mbuf();
		chunk[send_index].mbuf_state_flag=EMPTY;
		chunk_send_index=(chunk_send_index+1)%chunk_mbuf_num;
		printf("[%s]____chunk_send_index++\n",__func__);
		//getchar();

/*
                //Although it moves nothing ! But it tells the receiver that the File has been sent totally!
		if(the_last_block_flag==1)
		{
			move_recive_buf_to_send_buf();
			move_ok=1;
			the_last_block_flag=0;
		}
*/
	}
}

void *thread_printlog(void)
{
	int i=0,counter;
	while(1)
	{
		printf("\033[1;21;46m [ %s]%d \033[0m __send_ok=%d__move_ok=%d send_bysn_waiting_flag=%d\n",__func__,counter++,send_ok,move_ok,send_bysn_waiting_flag);	
		printf("	chunk_receive_index=%d  chunk_send_index=%d\n",chunk_receive_index,chunk_send_index);

		for(i=0;i<chunk_mbuf_num;i++)
		{
			printf("	chunk[%d].mbuf_state_flag=%d  ",i,chunk[i].mbuf_state_flag);
			Hop_header_t * hop_header =(Hop_header_t *)chunk[i].mbuf[0];
			printf("[ %s] block=%d  size=%d \n",__func__,hop_header->block_sequence,chunk[i].mbuf_piece_number);	
			
		}

		sleep(1);
	}
}

void *thread_recv(void * port_point)
{
	//int sockfd=Ethernet_InitSocket(PhysicalPort);
	port_t *port=(port_t *)port_point;
	
	socklen_t RecvSocketLen = 0;
	int RecvLength=0;
	uint8_t receive_temp_buf[BUFSIZE];
	
	uint64_t DATA_counter=0,BYSN_counter=0,ACK_counter=0;
	
	printf("\n [---Forwarding---] \n Waiting the sender to send the data!\n");
	
	while(1)
	{
		while(RecvLength = recvfrom(port->sockfd_receive, receive_temp_buf, RecvBufSize, 0, NULL, &RecvSocketLen))
		{
			Hop_header_t * hop_header =(Hop_header_t *)receive_temp_buf;
			if(memcmp(port->addr.addr_bytes,hop_header->ether_header.s_addr.addr_bytes,My_ETHER_ADDR_LEN)!=0&&hop_header->hop_ver==HOPv1)
			{
				if(hop_header->type==TYPE_DATA)
				{
					printf("[ %s]HOP_DATA[%ld]\n",__func__,++DATA_counter);
					process_DATA(hop_header);
				}
				else if(hop_header->type==TYPE_BYSN)
				{
					printf("[ %s]HOP_BYSN[%ld]\n",__func__,++BYSN_counter);
					process_BYSN(hop_header,port);
				}
				else if(hop_header->type==TYPE_ACK)
				{
					printf("[ %s]HOP_ACK[%ld]\n",__func__,++ACK_counter);
					process_ACK(hop_header,port);
				}
				else
				{
					//NOT FOUND!
				}
			}
		}
	}
	pthread_exit(NULL);
}


void *thread_receive_down(void * port_point)
{
	port_t *port=(port_t *)port_point;
	socklen_t RecvSocketLen = 0;
	int RecvLength=0;
	uint8_t receive_temp_buf[BUFSIZE];
	
	uint64_t DATA_counter=0,BYSN_counter=0,ACK_counter=0;
	
	while(1)
	{
		while(RecvLength = recvfrom(port->sockfd_receive, receive_temp_buf, RecvBufSize, 0, NULL, &RecvSocketLen))
		{
			Hop_header_t * hop_header =(Hop_header_t *)receive_temp_buf;
			if(memcmp(port->addr.addr_bytes,hop_header->ether_header.s_addr.addr_bytes,My_ETHER_ADDR_LEN)!=0&&hop_header->hop_ver==HOPv1)
			{
				
				
				if(hop_header->type==TYPE_DATA)
				{
					printf("[From %s]HOP_DATA[%ld]\n",__func__,++DATA_counter);
					process_DATA(hop_header);
				}
				else if(hop_header->type==TYPE_BYSN)
				{
					printf("[From %s]HOP_BYSN[%ld]\n",__func__,++BYSN_counter);
					process_BYSN(hop_header,port);
				}
				else if(hop_header->type==TYPE_ACK)
				{
					printf("[From %s]HOP_ACK[%ld]\n",__func__,++ACK_counter);
					process_ACK(hop_header,port);
				}
				else
				{
					//NOT FOUND!
				}
			}
		}
	}
	pthread_exit(NULL);
}


