#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include "ne.h"
#include "router.h"
#include "routingtable.c"
#include "endian.c"
#include <sys/select.h>
#include <sys/timerfd.h>

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

  // Initialize select
  fd_set rfds;  // Read File Descriptors
  int update_fd,converge_fd,failure_fd[init_resp.no_nbr];
  int maxfd = sockfd;
  int retval;

  // Declare timers
  struct itimerspec update_timer;
  struct itimerspec converge_timer;
  struct itimerspec failure_timer[init_resp.no_nbr];

  //Initialize timer intervals and values
  update_timer.it_value.tv_sec = UPDATE_INTERVAL;
  update_timer.it_value.tv_nsec = 0;
  converge_timer.it_value.tv_sec = CONVERGE_TIMEOUT;
  converge_timer.it_value.tv_nsec = 0;

  update_timer.it_interval.tv_sec = 0;
  update_timer.it_interval.tv_nsec = 0;
  converge_timer.it_interval.tv_sec = 0;
  converge_timer.it_interval.tv_nsec = 0;
  
  update_fd = timerfd_create(CLOCK_REALTIME, 0); 
  converge_fd = timerfd_create(CLOCK_REALTIME, 0); 

  maxfd = max(converge_fd, maxfd);
  maxfd = max(update_fd, maxfd);
  
  timerfd_settime(update_fd, 0, &update_timer, NULL);
  timerfd_settime(converge_fd, 0, &converge_timer, NULL);

  int i;
  for (i=0; i < init_resp.no_nbr; i++)
  {
    failure_timer[i].it_value.tv_sec = FAILURE_DETECTION;
    failure_timer[i].it_value.tv_nsec = 0;
    failure_timer[i].it_interval.tv_sec = 0;
    failure_timer[i].it_interval.tv_nsec = 0;
    failure_fd[i] = timerfd_create(CLOCK_REALTIME, 0);
    timerfd_settime(failure_fd[i], 0, &failure_timer[i], NULL);
    maxfd = max(failure_fd[i], maxfd);
  }

  while(1)
  {
    // Set rfds = 0
    FD_ZERO(&rfds);
    
    // Add all socket_fds to rfds
    FD_SET(sockfd, &rfds);
    FD_SET(update_fd, &rfds);
    FD_SET(converge_fd, &rfds);
    for (i=0; i < init_resp.no_nbr; i++)
    {
      FD_SET(failure_fd[i], &rfds);
    }

    retval = select(maxfd+1, &rfds, NULL, NULL, NULL);

    // UDP data received
    if(FD_ISSET(sockfd, &rfds))
    {
      n = recvfrom(sockfd, (struct pkt_RT_UPDATE *) &recv_update, sizeof(recv_update), 0, NULL, NULL);
      if (n < 0)
      {
        error("ERROR in recvfrom");
      }
      ntoh_pkt_RT_UPDATE(&recv_update);
      
      for(i = 0; i < NumRoutes; i++)
      {  
        if(routingTable[i].dest_id == recv_update.sender_id)
        {
          if(UpdateRoutes(&recv_update, routingTable[i].cost, router_id))
          {
            PrintRoutes(Logfile, router_id);
            
            // Reset Convergence timer
            converge_timer.it_value.tv_sec = CONVERGE_TIMEOUT;
            converge_timer.it_value.tv_nsec = 0;
            converge_timer.it_interval.tv_sec = 0;
            converge_timer.it_interval.tv_nsec = 0;
            timerfd_settime(converge_fd, 0, &converge_timer, NULL);
          }
          // Reset Fail timeout for neighbor that just sent me the update
          failure_timer[i].it_value.tv_sec = FAILURE_DETECTION;
          failure_timer[i].it_value.tv_nsec = 0;
          failure_timer[i].it_interval.tv_sec = 0;
          failure_timer[i].it_interval.tv_nsec = 0;
          timerfd_settime(failure_fd[i], 0, &failure_timer[i], NULL);

          fflush(Logfile);
        }
      }
    }

    // UPDATE_TIMEOUT
    if(FD_ISSET(update_fd, &rfds))
    {
      // Clear send_update and convert Routing table to a packet
      memset(&send_update, 0, sizeof(send_update));
      ConvertTabletoPkt(&send_update, router_id);
      for (i=0; i < init_resp.no_nbr; i++)
      {
        send_update.dest_id = init_resp.nbrcost[i].nbr;
        hton_pkt_RT_UPDATE(&send_update);

        n = sendto(sockfd, (struct pkt_RT_UPDATE *) &send_update, sizeof(send_update), 0, (struct sockaddr *) &neaddr, sizeof(neaddr));
        if (n < 0)
        { 
          error("ERROR in sendto");
        }
        ntoh_pkt_RT_UPDATE(&send_update);
      }

      update_timer.it_value.tv_sec = UPDATE_INTERVAL;
      update_timer.it_value.tv_nsec = 0;
      update_timer.it_interval.tv_sec = 0;
      update_timer.it_interval.tv_nsec = 0;
      timerfd_settime(update_fd, 0, &update_timer, NULL);
      fflush(Logfile);
    }

    // CONVERGENCE_TIMEOUT
    if(FD_ISSET(converge_fd, &rfds))
    {

    }

    // NEIGHBOR_FAILURE_TIMEOUT
    for (i=0; i < init_resp.no_nbr; i++)
    {
      if(FD_ISSET(failure_fd[i], &rfds))
      {

      }
    }
  }





  return 0;
}
