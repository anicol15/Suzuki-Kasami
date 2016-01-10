#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>

#define MAX_PROCESSES_IN_QUEUE 50 //the maximum number if clients that can wait in queu to connect to the serve
#define MAXBUFFERSIZE 4096 //the maximum size of the buffer
#define N 3
#define perror2(s,e) fprintf(stderr, "%s:%s\n" , s, strerror(e))
#define MAX_THREADS_NO 100

pthread_mutex_t locker; //Lock the strtok

pthread_t *tids;
int port, server_socket, counter, server_id;
int *addr; //represents shared memory as int
char buf[MAXBUFFERSIZE];
int ports[N], req[N], last[N], Q[N], sockets[N];
char ip_addresses[N][120];
int Id, err, hasToken, get_a_request, reqs_before_broadcast, reqs_after_finished, total_difference_reqs;

/*
 * Read input from file
 */
void read_input_file()
{
	printf("read_input_file");
	FILE *fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	char *ch;
	int i=0;
	Id = -1;
	fp = fopen("input.txt", "r");
	if (fp == NULL)
	{
		printf("Error not exist input file");
		exit(1);
	}

	if(err=pthread_mutex_lock(&locker))
	{
		perror2("Failed to lock()",err);
	}
	while ((read = getline(&line, &len, fp)) != -1)
	{
		strcpy(ip_addresses[i], strtok(line, " "));
		ports[i] = atoi(strtok(NULL, " ,"));
		if(ports[i] == port)
			Id = i;//find the id of this node
		i++;
	}
	if(err=pthread_mutex_unlock(&locker))
	{
		perror2("Failed to lock()",err);
	}
	printf("id %d ", Id);

	if(Id == -1)
		exit(1);

	fclose(fp);
	if (line)
		free(line);
}

/*
 * Handler of signal SIGINT
 */
void handlersigint()
{
	printf("\n in handlersigint");
	close(server_socket);
	int i;
	for(i=0; i<N; i++)
	{
		close(sockets[i]);
	}
	exit(0);
}

/*
 * Returns a random number between min and max
 */
int getrand(int min,int max)
{
	srand(time(NULL));
	return(rand()%(max-min)+min);
}

/*
 * Returns the information of the server
 */
char *get_server_name(int port)
{
	char server_get_name[256];

	//get the host name of the current machine
	gethostname(server_get_name, 256);

	struct hostent *host = gethostbyname(server_get_name);
	//printf("\n ******* %s", host->h_name);
	return host->h_name;
}

/*
 * Read the message-request from another node and updates the table req
 */
void *thread_accept_a_req(void *param)
{
	int new_get_req_socket =( (int) param) ;
	//printf("in thread accept %d  req socket %d",pthread_self(), new_get_req_socket );

	int msg_size;
	char strres[4096];
	char *token;
	char newBuf[4096];
	//get request from another process
	signal(SIGINT, handlersigint);

	bzero(newBuf, sizeof(newBuf));
	printf("\nBefore recv\n");
	//receive a number from the client
	if((msg_size = recv(new_get_req_socket, newBuf, sizeof(newBuf), 0)) < 0)
	{
		perror("Error while reading at server");
		close(new_get_req_socket);
		exit(1);
	}
	printf("After recv , msg:%s\n", newBuf);

	if(err=pthread_mutex_lock(&locker))
	{
		perror2("Failed to lock()",err);
	}

	//get request
	//get the number n and k from the buffer, seperated by underscore character
	int msg_type = atoi(strtok(newBuf, "_"));
	printf("\n msg type: %d \n", msg_type);

	if(msg_type == 1)
	{

		int requested_node = atoi(strtok(NULL, "_"));
		int req_requested_node = atoi(strtok(NULL, "_"));
		printf("Msg from node %d req %d", requested_node, req_requested_node);
		//update table req
		if (req[requested_node] <= req_requested_node) {
			printf("update req %d %d", req[requested_node], req_requested_node);
			req[requested_node] = req_requested_node;

			int i, already_in=0;
			//update table Q if the process has the token
			if (hasToken == 1)
			{
				get_a_request = 1;
				//already in the queue the requested node?
				for (i = 0; i < N; i++)
				{
					if(Q[i] == requested_node)
					{
						already_in = 1;
					}
				}
				//if is not already in the queue the requested node, inserts it to
				//the first empty place
				if(already_in == 0) {
					for (i = 0; i < N; i++) {
						if (Q[i] == -1) {
							Q[i] = requested_node;
							printf("\n Insert node %d in queue", Q[i]);
							break;
						}
					}
				}
			}
		}

	}
	else //msg_type=2
	{
		printf("\n process %d get the Token \n", Id);
		hasToken = 1;
		char* last_input;
		last_input = strtok(NULL, "_");
		printf("\nMessage body last: %s",last_input );
		char* q_input;
		q_input = strtok(NULL, "_");
		printf("\nMessage body q: %s",q_input );
		int i;

		//initialize table last
		last[0] = atoi(strtok(last_input, ","));
		//printf("\ni: %d, last[i] %d",0,last[0]);
		for(i=1;i<N;i++)
		{
			last[i] = atoi(strtok(NULL, ","));
			//printf("\ni: %d, last[i] %d",i,last[i]);
		}

		//initialize table Q
		Q[0] = atoi(strtok(q_input, ","));
		if(Q[0] == Id)
		{
			Q[0] = -1;
		}
		if(Q[0]!= -1) {
			get_a_request = 1;
		}
		//printf("\ni: %d, Q[i] %d",0,Q[0]);
		for(i=1;i<N;i++)
		{
			Q[i] = atoi(strtok(NULL, ","));
			if(Q[i] == Id)
			{
				Q[i] = -1;
			}
			//printf("\ni: %d, Q[i] %d",i,Q[i]);
			if(Q[i]!= -1) {
				get_a_request = 1;
			}
		}

	}
	if(err=pthread_mutex_unlock(&locker))
	{
		perror2("Failed to lock()",err);
	}


	close(new_get_req_socket);
	sleep(2);

	pthread_exit(0);
}

/*
 * Create a thread to create a socket to get the requests from other nodes
 * and for each request create another thread to handle it
 */
void *thread_accept_reqs()
{
	struct sockaddr_in server_address, client_address; //Server and client address
	struct sockaddr *serverptr, *clientptr;
	struct hostent *client_host;
	int client_address_length, server_address_length;
	pthread_t inner_tid;
	//printf("Trigger accept_reqs.... %d\n", pthread_self() );
	//struct data *temp_try =(struct data*) arg;

	//Create socket
	if ((server_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Error while initalizing the initial socket of the node");
		exit(1);
	}


	//set server_address struct
	server_address.sin_family = PF_INET; // Internet domain
	server_address.sin_addr.s_addr = htonl(INADDR_ANY); // IP address of this machine
	server_address.sin_port = htons(port); // The input port
	serverptr = (struct sockaddr *) &server_address;
	int serverlen = sizeof (server_address);

	// Bind socket to address
	if (bind(server_socket, serverptr, serverlen) < 0)
	{
		perror("Error while setting the initial socket of the node to bind in order to associate the socket that was created with its address");
		close(server_socket);
		exit(1);
	}
	//Set the server to listen for connections
	if (listen(server_socket, MAX_PROCESSES_IN_QUEUE ) < 0)
	{
		perror("Error while setting the initial socket of the node to listen for clients requests");
		close(server_socket);
		exit(1);
	}
	printf("\nListening for connections to port %d........\n", port);

	int new_socket, status;
	while(1)
	{
		//initialize of client

		clientptr = (struct sockaddr *) &client_address;
		client_address_length = sizeof(client_address);


		//Accept request from client
		if ( (new_socket = accept(server_socket, clientptr, &client_address_length )) == -1)
		{
			printf("accept");
			close(server_socket);
			exit(1);
		}
		printf("Accept request message.... \n");
		int i;
		
		if(err=pthread_create(&inner_tid, NULL , thread_accept_a_req , (void*) new_socket))
		{
			perror2("pthread_create" , err);
			exit(1);
		}

		if (err = pthread_join( inner_tid, (void **) &status))
		{
			perror2("pthread_join", err); exit(1);
		}

		printf("\n**requests after\n");
		for(i=0;i<N;i++)
		{
			printf("req[%d] = %d \n", i, req[i]);
		}

	}

}

/*
 * Returns the total number of requests from other nodes at the specific time
 */
int sum_reqs()
{
	int i;
	int sum = 0;
	for(i=0;i<N;i++) {
		if(i!=Id) {
			sum = sum + req[i];
		}
	}
	return sum;
}


/*
 * Creates the connection to each other node through which this node will send its request to access the critical section
 * and broadcast its request to all other nodes
 */
int connect_to_other_nodes() {

	struct hostent *server_host;
	struct sockaddr_in server_address;
	struct sockaddr *serverptr;
	int i;
	char newbuf[4096];

	if (hasToken == 0) {
		//broadcast request

		for (i = 0; i < N; i++) {
			if (i != Id) {
				//Create socket for client
				if ((sockets[i] = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
					printf("Error while initalizing the initial socket of the client");
					exit(1);
				}

				//Find server's address from its name
				server_host = gethostbyname(ip_addresses[i]);


				if (server_host == NULL) {
					printf("Error while getting the host of client from its name");
					exit(1);
				}

				//set server address
				server_address.sin_family = AF_INET;        //Internet address family
				//bcopy((char *) server_host -> h_addr, (char *) &server_address.sin_addr, server_host -> h_length);
				server_address.sin_addr.s_addr = htonl(INADDR_ANY);
				server_address.sin_port = htons(ports[i]);    //Server's Internet address and port
				//	printf("/n sockts : port %d",ports[i]);
				serverptr = (struct sockaddr *) &server_address;
				int serverlen = sizeof(server_address);

				//Connect with server
				if ((connect(sockets[i], serverptr, serverlen)) == -1) {
					printf("Establish connection failed i %d ports %d \n", i, ports[i]);
					exit(1);
				}
			}

		}

		//send a req to other nodes

		req[Id] = req[Id] + 1;

		reqs_before_broadcast = sum_reqs();

		//broadcast a request to all nodes
		for (i = 0; i < N; i++) {

			if (Id != i) {
				bzero(newbuf, sizeof(newbuf));
				sprintf(newbuf, "%d_%d_%d", 1, Id, req[Id]);
				printf("\nNode: %d going to send to:%d, msg:%s\n", Id, i, newbuf);
				//printf("send to %d : i: %d", sockets[i], i);
				if (send(sockets[i], newbuf, sizeof(newbuf), 0) < 0) {
					perror("Error while writting at client");
					//close(sockets[i]);
					exit(1);
				}
				printf("\nNode: %d send to: %d  %s \n", Id, i, newbuf);

			}
		}
		for (i = 0; i < N; i++) {
			if (Id != i) {
				{
					close(sockets[i]);
				}
			}

		}

	}//has_token=0
	printf("\nprocess %d wait for token", Id);
	int msg_size;
	char recvBuf[4096];
	//wait for token
	while (hasToken == 0)
	{
		sleep(1);
	}

	printf("\nprocess %d access the critical section\n", Id);
	reqs_after_finished = sum_reqs();

	int difference = reqs_after_finished - reqs_before_broadcast;
	printf("\n reqs_before_broadcast %d reqs_after_finished: %d Difference %d",reqs_before_broadcast,reqs_after_finished,  difference);
	//access critical section
	sleep(getrand(5, 7));
	last[Id] = req[Id];
	printf("\n my last[%d] = %d", Id, last[Id]);
	for(i=0;i<N;i++)
	{
		printf("\n Q[%d] = %d", i, Q[i]);
	}





	while(get_a_request ==0); //wait to get a request from another node

	for(i=0;i<N;i++)
	{
		printf("\n last[%d] = %d", i, last[i]);
	}

	//send token to the first node of Q
	int socket_send_token;
	for(i=0; i < N; i++)
	{
		if( (Q[i]!=-1) && (Q[i] != Id))
		{
			//send token to i
			printf("\nsend token to %d", Q[i]);
			//connect
			//Create socket for client
			if((socket_send_token = socket(AF_INET, SOCK_STREAM, 0))==-1)
			{
				printf("Error while initalizing the initial socket of the client");
				exit(1);
			}

			//Find server's address from its name
			server_host = gethostbyname(ip_addresses[Q[i]]);


			if (server_host == NULL)
			{
				printf("Error while getting the host of client from its name");
				exit(1);
			}

			//set server address
			server_address.sin_family = AF_INET;		//Internet address family
			//bcopy((char *) server_host -> h_addr, (char *) &server_address.sin_addr, server_host -> h_length);
			server_address.sin_addr.s_addr=htonl(INADDR_ANY);
			server_address.sin_port = htons(ports[Q[i]]);	//Server's Internet address and port
			//	printf("/n sockts : port %d",ports[i]);
			serverptr = (struct sockaddr *) &server_address;
			int serverlen = sizeof (server_address);

			//Connect with server
			if((connect(socket_send_token,serverptr,serverlen))==-1)
			{
				printf("Establish connection failed i %d ports %d \n", i, ports[Q[i]]);
				exit(1);
			}

			//create 2 strings for the table last and table Q
			char strlast[2048];
			char strlasttemp[512];
			char strqueue[2048];
			char strqueuetemp[512];
			bzero(strlasttemp,sizeof(strlasttemp));
			bzero(strlast,sizeof(strlast));
			bzero(strqueuetemp,sizeof(strqueuetemp));
			bzero(strqueue,sizeof(strqueue));
			int j,k;
			/*
			printf("\nsee Q");
			for(j=0; j<N; j++)  //start to create the Q string from the position 1 because
				//the position 0 will be the node that will take the token
			{
				printf("\nQ[%d] = %d,", j, Q[j]);
			}
			 */

			for(j=0; j<N; j++)
			{

				if(j>=1)
				{
					sprintf(strlasttemp, ",%d", last[j]);

				}
				else
				{
					sprintf(strlasttemp, "%d", last[j]);
				}
				strcat(strlast,strlasttemp);

			}

			//check if any process has req>last and is not in Q
			for(j=0; j<N; j++)
			{
				if(req[j]>last[j] )
				{
					//check if j is in the Q
					int in_Q=0;
					for(k=0;k<N;k++)
					{
						if(Q[k] == j)
							in_Q=1;
					}

					//if is not in Q, then insert it
					if(in_Q==0) {
						for (k = 0; k < N; k++) {
							if (Q[k] == -1)
								Q[k] = j;
						}
					}
				}

			}


			for(j=1; j<N; j++)  //start to create the Q string from the position 1 because
				//the position 0 will be the node that will take the token
			{
				sprintf(strqueuetemp, "%d,", Q[j]);
				strcat(strqueue,strqueuetemp);
			}


			sprintf(strqueuetemp, "%d", -1);
			strcat(strqueue,strqueuetemp);


			bzero(newbuf,sizeof(newbuf));
			int msg_type = 2;
			sprintf( newbuf , "%d_%s_%s", msg_type,strlast,strqueue);

			if( send(socket_send_token, newbuf, sizeof(newbuf), 0 ) < 0)
			{
				perror("Error while writting at client");
				close(socket_send_token);
				exit(1);
			}
			printf("\n**Send Token: Node: %d going to send to:%d, msg:%s\n" , Id , Q[i], newbuf);


			//initialize again tables Q and last, and var get_a_request, hasToken
			for(i=0; i<N; i++)
			{
				Q[i] = -1;
				last[i] = -1;
			}
			get_a_request = 0;
			hasToken = 0;

			break;
		}
	}

}


int main ( int argc, char *argv[] ) {
	int status, i;
	pthread_t tid, tid_send_req;
	get_a_request = 0;
	total_difference_reqs = 0;
	pthread_mutex_init(&locker,NULL);

	for (i = 0; i < N; i++) {
		req[i] = 0;
		last[i] = -1;
		Q[i] = -1;
		sockets[i] = 0;
	}
	req[0] = 1;

	server_id = getpid();

	signal(SIGINT, handlersigint);

	if (argc != 3) {
		perror("Wrong number of arguments! Please give as argument the port number!\n");
		exit(1);
	}

	//read parameter
	port = atoi(argv[2]);

	read_input_file();
	if (Id == 0) {
		hasToken = 1;
	}
	else {
		hasToken = 0;
	}

	sleep(20);
	//create a thread to get the requests from other nodes
	if (err = pthread_create(&tid, NULL, thread_accept_reqs, (void *) NULL)) {
		perror2("pthread_create", err);
		exit(1);
	}
	sleep(20);


	while(1) {
		//each node create a socket with each other node through which it will send its requests
		connect_to_other_nodes();
	
		sleep(getrand(6, 15));

	}


}

