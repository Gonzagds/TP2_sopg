#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SerialManager.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#define SERIAL_PORT		1
#define BAUD_RATE		115200
#define SERIAL_SLEEP 	10000//microsegundos
#define TCP_PORT		"10000"

void* serial_thread(void* msg);
void sigint_handler(int sig); /* prototype */
int establish_connection(int* listener_fd, int* connected_fd);
int procesa_trama_serie(char* buf, int len, int* offset);
int procesa_trama_tcp(char* buf, int len,int* states, int* offset);

pthread_t serial_thread_hand;
_Atomic int flag_end;


void sigint_handler(int sig)
{
	if(sig == SIGINT || sig == SIGTERM)
	{
		flag_end = 1;
	}	
}

int main(void)
{
	int ret;
	struct sigaction sa;
	int listener_fd, connected_fd;

	printf("Inicio Serial Service\r\n");
	printf("Id Proceso %u...\r\n", getpid());
		
	sa.sa_handler = sigint_handler;
	sa.sa_flags = 0; // or SA_RESTART
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) 
	{
		perror("sigaction");
		exit(1);
	}

	if(establish_connection(&listener_fd, &connected_fd) ==-1)
	{
		perror("Unable to open socket connection");
		exit(1);
	}

	if(serial_open(SERIAL_PORT,BAUD_RATE))
	{
		close(listener_fd);
		close(connected_fd);
		perror("Unable to open serial port");
		exit(1);
	}

	flag_end = 0;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &set,NULL);

	//todo: manejar errores de no creación de hilos
	ret = pthread_create(&serial_thread_hand,
					NULL,
					serial_thread,
					(void*)&connected_fd);
	if(ret)
	{
		close(listener_fd);
		close(connected_fd);
		serial_close();
		perror("Unable to create new thread");
		exit(1);
	}

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_UNBLOCK, &set,NULL);

	//Aquí procesaremos las tramas entrantes desde interface service
	//y enviamos por puerto serie
	char buf_in[50];
	char buf_out[50];
	char* ptr = buf_in;
	int rec_bytes = 0;
	int states[4];
	
	while(1)
	{
		rec_bytes = recv(connected_fd, ptr, sizeof(buf_in)-(ptr-buf_in),0);

		if(rec_bytes <= 0)
			break;

		ptr += rec_bytes;
		if(ptr-buf_in >= sizeof(buf_in)-1) //ver si falta un -1
		{
			ptr = buf_in;
			memset(buf_in,0, sizeof(buf_in));
		}

		//esto debería estar en un bucle para consumir todas las tramas del buffer
		//que pudieran llegar juntas
		if( procesa_trama_tcp(buf_in, sizeof(buf_in), states, &rec_bytes) == 0)
		{
			sprintf(buf_out,">OUTS:%u,%u,%u,%u\r\n", states[0],states[1],states[2],states[3]);
			
			serial_send( buf_out, strlen(buf_out));
		}
		//se corre el puntero en la cantidad de bytes eliminados 
		//por procesa_trama
		ptr -= rec_bytes;

		//flag de finalización hilo serie
		if(flag_end)
			break;
	}


	pthread_cancel(serial_thread_hand);
	//esperamos la finalización de los hilos que lanzamos
	pthread_join(serial_thread_hand,NULL);	

	close(listener_fd);
	close(connected_fd);
	serial_close();

	exit(EXIT_SUCCESS);
	return 0;
}

/*
Funcion que encapsula la creación y apertura de la conexión TCP.
Por ahora solo retorna -1 en error pero se podría hacer un 
retorno de errores mas completo
*/
int establish_connection(int* listener_fd, int* connected_fd)
{
	struct addrinfo hints;
	struct addrinfo *result;
	struct sockaddr_storage their_addr;
	int addr_size;

	memset((void*)&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; 

	int r = getaddrinfo(NULL,TCP_PORT,&hints,&result);
	
	if (r != 0)
	{		
		return -1;
	}

	*listener_fd = socket(result->ai_family, result->ai_socktype, 0);
	if(*listener_fd == -1)
	{
		return -1;
	}
	
	if (bind(*listener_fd, (struct sockaddr*)result->ai_addr,result->ai_addrlen) == -1) 
	{
		close(*listener_fd);		
		return -1;
	}
	freeaddrinfo(result);

	if (listen(*listener_fd, 2) == -1)
	{
		return -1;
	}

	addr_size = sizeof(their_addr);
	*connected_fd = accept(*listener_fd, (struct sockaddr *)&their_addr, &addr_size);

	if(*connected_fd == -1)
	{		
		return -1;
	}

	return 0;
}


void* serial_thread(void* msg)
{
	int* sock_connect = (int*) msg;
	char buf_in [100];
	char buf_out[40];
	char* ptr = buf_in;
	int num_bytes = 0;
	int button;

	printf("inica hilo atención serie\n");
	memset(buf_in,0, sizeof(buf_in));
	//aquí procesaremos las tramas entra ntes desde EDUCIAA
	while(1)
	{
		num_bytes = sizeof(buf_in) - (ptr - buf_in);

		num_bytes = serial_receive(ptr,num_bytes);
		if(num_bytes < 0)
			break;
		if(num_bytes > 0)
		{
			printf("%s",buf_in);
		}
			

		ptr += num_bytes;
		if(ptr-buf_in >= sizeof(buf_in)-1)
		{
			ptr = buf_in;
			memset(buf_in,0, sizeof(buf_in));
		}

		while(1)
		{
			button = procesa_trama_serie(buf_in, sizeof(buf_in), &num_bytes);
			ptr -= num_bytes;

			if(button < 0)
				break;					
			sprintf(buf_out,":LINE%iTG\n", button);
			printf("%s",buf_out);
			//aca deberíamos verificar si se envío toda la trama
			send(*sock_connect, buf_out, strlen(buf_out), 0);			
		}
		usleep(SERIAL_SLEEP);
	}

	flag_end = 1;

	pthread_exit(NULL);
	return NULL;
}


/*
buf: buffer a proceasar
len: largo del buffer
offset: retorna cuanto se corre el buffer al descartar datos procesados

retorna el numero de tecla recibida si hay una trama correcta
		-1 si no hay trama correcta 
*/
int procesa_trama_serie(char* buf, int len, int* offset)
{
	//012345678901234 5 6
	//>TOGGLE STATE:X\r\n
	#define LEN_STRING_CIAA 17 //>TOGGLE STATE:X\r\n
	char* char_ini;
	char* char_end;
	int i;
	int ret = -1;

	*offset = 0;

	char_ini = NULL;
	//busco caracter de inicio
	for(i = 0; i < len ; i++)
	{
		if(*(buf+i) == '>')
		{
			char_ini = buf + i;
			break;
		}
	}
	if(char_ini == NULL)
		return -1;

	//elimino todo lo que esta antes del caracter de inicio de ser necesario
	if(buf != char_ini)
	{
		memcpy(buf, char_ini, len - i);
		*offset += (int) (char_ini - buf);
	}
		

	char_end = NULL;
	//busco caracter de fin desde donde se encontro el de inicio
	for(i = 0; i < len ; i++)
	{
		if(*(buf+i) == '\n')
		{
			char_end = buf + i;
			break;
		}
	}
	if(char_end == NULL)
		return -1;

	//compruebo largo de cadena
	if(char_end - buf != (LEN_STRING_CIAA-1))
	{
		//cadena de lardo incorrecto
		//elimino todo lo que esta antes del caracter de fin
		memcpy(buf, char_end, len - i);
		*offset += (int) (char_end - buf);
		return -1;
	}
	//faltaria verificar el resto de la cadena
	//verificos valores validos
	if((buf[14] >= '0' && buf[14] <= '3') )
	{
		ret = (int) (buf[14] - '0');		
	}
	else
	{
		ret = -1;
	}
		
	memcpy(buf, char_end, len - i);
	*offset += (int) (char_end - buf);

	return ret;	
}

/*
buf: buffer a proceasar
len: largo del buffer
states: puntero a un array de enteros donde se devolveran los estados a de las salidas
offset: retorna cuanto se corre el buffer al borrar datos ya procesados

retorna 0 si hay una trama correcta
		-1 si no hay trama correcta o entera
*/
int procesa_trama_tcp(char* buf, int len,int* states, int* offset)
{
	#define LEN_STRING 12 //:STATESXYWZ\n
	char* char_ini;
	char* char_end;
	int i;

	*offset = 0;

	char_ini = NULL;
	//busco caracter de inicio
	for(i = 0; i < len ; i++)
	{
		if(*(buf+i) == ':')
		{
			char_ini = buf + i;
			break;
		}
	}
	if(char_ini == NULL)
		return -1;

	//elimino todo lo que esta antes del caracter de inicio de ser necesario
	if(buf != char_ini)
	{
		memcpy(buf, char_ini, len - i);
		*offset += (int) (char_ini - buf);
	}
		

	char_end = NULL;
	//busco caracter de fin desde donde se encontro el de inicio
	for(i = 0; i < len ; i++)
	{
		if(*(buf+i) == '\n')
		{
			char_end = buf + i;
			break;
		}
	}
	if(char_end == NULL)
		return -1;

	//compruebo largo de cadena
	if(char_end - buf != LEN_STRING-1)
	{
		//cadena de lardo incorrecto
		//elimino todo lo que esta antes del caracter de fin
		memcpy(buf, char_end, len - i);
		*offset += (int) (char_end - buf);
		return -1;
	}
	//faltaria verificar el resto de la cadena
	//verificos valores validos
	if((buf[7] >= '0' && buf[7] <= '2') &&
		(buf[8] >= '0' && buf[8] <= '2') &&
		(buf[9] >= '0' && buf[9] <= '2') &&
		(buf[10] >= '0' && buf[10] <= '2'))
	{
		states[0] = buf[7] - '0';
		states[1] = buf[8] - '0';
		states[2] = buf[9] - '0';
		states[3] = buf[10] - '0';

		memcpy(buf, char_end, len - i);
		*offset += (int) (char_end - buf);
		return 0;
	}
		
	memcpy(buf, char_end, len - i);
	*offset += (int) (char_end - buf);

	return -1;	
}
