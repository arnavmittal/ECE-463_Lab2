#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include "ne.h"
#include "router.h"
#include "routingtable.c"
#include "endian.c"
#include <sys/select.h>

#define max(a,b)   (((a) > (b)) ? (a) : (b))

char *concatenate(char *a, char *b, char *c)
{
    int size = strlen(a) + strlen(b) + strlen(c) + 1;
    char *str = malloc(size);
    strcpy (str, a);
    strcat (str, b);
    strcat (str, c);

    return str;
}

#define INT_DIGITS 19   /* enough for 64 bit integer */

char *itoa(i)
     int i;
{
  /* Room for INT_DIGITS digits, - and '\0' */
  static char buf[INT_DIGITS + 2];
  char *p = buf + INT_DIGITS + 1; /* points to terminating '\0' */
  if (i >= 0) {
    do {
      *--p = '0' + (i % 10);
      i /= 10;
    } while (i != 0);
    return p;
  }
  else {      /* i < 0 */
    do {
      *--p = '0' - (i % 10);
      i /= 10;
    } while (i != 0);
    *--p = '-';
  }
  return p;
}

int main(int argc, char **argv) 
{

  int router_id, ne_port, router_port;
  char *ne_host;


  if(argc < 5)
  {
    printf("usage: router <router id> <ne_host> <ne_port> <router_port>\n");
  }
  
  printf("Init start\n");

  // Initialize arguements from the Command Line
  router_id = atoi(argv[1]);
  ne_host = argv[2];
  ne_port = atoi(argv[3]);
  router_port = atoi(argv[4]);
  
  //----------------------------------------- Initialization Start -------------------------------------------//
  int sockfd; // socket
  struct sockaddr_in serveraddr, neaddr; // server's addr
  int optval; // flag value for setsockopt

  // 
  // socket: create the parent socket 
  //
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  // setsockopt: Handy debugging trick that lets 
  // us rerun the server immediately after we kill it; 
  // otherwise we have to wait about 20 secs. 
  // Eliminates "ERROR on binding: Address already in use" error. 
  //
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
       (const void *)&optval , sizeof(int));

  //
  // build the server's Internet address
  //
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned int)router_port);

  //
  // build the network emulator's Internet address
  //
  bzero((char *) &neaddr, sizeof(neaddr));
  neaddr.sin_family = AF_INET;
  inet_aton(ne_host, &neaddr.sin_addr);
  neaddr.sin_port = htons((unsigned int)ne_port);

  // 
  // bind: associate the parent socket with a port 
  //
  if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
    error("ERROR on binding");

  //----------------------------------------- Initialization Done --------------------------------------------//

  printf("Init Done\n");

  //------------------------------------------ Send INIT_REQUEST ---------------------------------------------// 
  int n;
  struct pkt_INIT_REQUEST init_req;
  
  init_req.router_id = htonl(router_id);

  n = sendto(sockfd, (struct pkt_INIT_REQUEST *) &init_req, sizeof(init_req), 0, (struct sockaddr *) &neaddr, sizeof(neaddr));
  if (n < 0)
  { 
    error("ERROR in sendto");
  }

  //------------------------------------------ INIT_REQUEST Sent ---------------------------------------------// 
  
  printf("Sent Request\n");

  //---------------------------------------- Receive INIT_RESPONSE -------------------------------------------// 
  struct pkt_INIT_RESPONSE init_resp;
  
  n = recvfrom(sockfd, (struct pkt_INIT_RESPONSE *) &init_resp, sizeof(init_resp), 0, NULL, NULL);
  if (n < 0)
  {
    error("ERROR in recvfrom");
  }
  //--------------------------------------- INIT_RESPONSE Received -------------------------------------------// 

  printf("Received INIT_RESPONSE from %d\n", router_id);

  //-------------------------------------- Initialize Routing Table ------------------------------------------//  
  
  ntoh_pkt_INIT_RESPONSE((struct pkt_INIT_RESPONSE *) &init_resp);
  InitRoutingTbl((struct pkt_INIT_RESPONSE *) &init_resp, router_id);
  
  FILE* Logfile;
  char * filename;
  filename = concatenate("router", itoa(router_id), ".log");
  
  Logfile = fopen(filename, "w");
  PrintRoutes(Logfile, router_id);
  
  //------------------------------------- Routing Table Initialized ------------------------------------------//  

  //---------------------------------------- Select Timer Stuff ----------------------------------------------// 
  
  // Initializing RT_UPDATE
  struct pkt_RT_UPDATE send_update, recv_update;

  // Select timer

  /*  Wait for:
  1) sockfd = UDP fd
  2) UPDATE_INTERVAL
  3) FAILURE_DETECTION (3*UPDATE_INTERVAL)
  4) CONVERGENCE_TIMEOUT

  */
  // Variable declaration for select()
  fd_set  rset;
  int maxfdp, nready;
  
  // Initialyze for select()
  FD_ZERO(&rset);
  maxfdp1 = max(listenfd, sockfd) + 1;

  while (1) {

    FD_SET(listenfd, &rset);
    FD_SET(sockfd, &rset);

    if((nready = select(maxfdp, &rset, NULL, NULL, NULL)) < 0) 
    {
      if (errno == EINTR)
      {
        continue;
      }
      else
      {
        err_sys("select error");
      }
    }

    if (FD_ISSET(listenfd, &rset)) 
    {
      printf ("HTTP Recvd\n");
    }
  return 0;
}