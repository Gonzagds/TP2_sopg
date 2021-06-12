#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SerialManager.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#define SERIAL_PORT		1
#define BAUD_RATE		115200
#define SERIAL_SLEEP 	10000//microsegundos
#define TCP_PORT		"10000"

void* serial_thread(void* msg);
void* tcp_thread(void* msg);
void sigint_handler(int sig); /* prototype */

pthread_t serial_thread_hand;


void sigint_handler(int sig)
{
	if(sig == SIGINT || sig == SIGTERM)
	{
		pthread_cancel(serial_thread);		
	}
	
}

int main(void)
{
	int ret;
	struct sigaction sa;
	struct addrinfo hints;
	struct addrinfo result;
	int sock_fd;

	
	printf("Inicio Serial Service\r\n");
		
	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0; // or SA_RESTART
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) 
	{
		perror("sigaction");
		exit(1);
	}

	memset((void*)&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; 

	int r = getaddrinfo(NULL,TCP_PORT,&hints,&result);
	
	if (r != 0)
	{
		fprintf(stderr,"getaddrinfo:%s",gai_strerror(r));
		exit(1);
	}

	sock_fd = socket(result->ai_family, result->ai_socktype, 0);
	if(sock_fd == -1)
	{
		perror("couldn't create socket");
		exit(1);
	}
	
	if (bind(sock_fd, (struct sockaddr*)result->ai_addr,result->ai_addrlen) == -1) 
	{
		close(sock_fd);
		perror("listener: bind");
		exit(1);
	}
	freeaddrinfo(result);

	if (listen(sock_fd, 2) == -1)
	{
		exit(1);
	}

	int open_sock_fd;
	struct sockaddr_storage their_addr;
	int addr_size = sizeof(their_addr);
	open_sock_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &addr_size);

	if(open_sock_fd == -1)
	{
		perror("error accepting incoming conection");
		exit(1);
	}

	if(serial_open(SERIAL_PORT,BAUD_RATE))
	{
		close(open_sock_fd);
		close(sock_fd);
		perror("Unable to open serial port");
		exit(1);
	}

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &set,NULL);

	//todo: manejar errores de no creación de hilos
	ret = pthread_create(&serial_thread_hand,
					NULL,
					serial_thread,
					NULL);
	if(ret)
	{
		close(open_sock_fd);
		close(sock_fd);
		exit(ret);
	}

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_UNBLOCK, &set,NULL);

	//Aquí procesaremos las tramas entrantes desde interface service
	while(1)
	{


	}
	//esperamos la finalización de los hilos que lanzamos
	pthread_join(serial_thread, NULL);
	pthread_join(tcp_thread, NULL);
	exit(EXIT_SUCCESS);
	return 0;
}

void* serial_thread(void* msg)
{
	char buf[50];
	int num_bytes;

	//aquií procesaremos las tramas entrantes desde EDUCIAA
	while(1)
	{
		num_bytes = serial_receive(buf,sizeof(buf));
		if(num_bytes <= 0)
			break;
		procesa_trama_serie(buf);
		//enviar por TCP
		usleep(SERIAL_SLEEP);
	}

	return NULL;
}

void procesa_trama_serie(char* buf)
{

}
