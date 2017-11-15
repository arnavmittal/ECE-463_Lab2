#include "ne.h"
#include "router.h"
#include <stdio.h>
#include <stdlib.h>

struct route_entry routingTable[MAX_ROUTERS];
int NumRoutes;

/* Routine Name    : InitRoutingTbl
 * INPUT ARGUMENTS : 1. (struct pkt_INIT_RESPONSE *) - The INIT_RESPONSE from Network Emulator
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine is called after receiving the INIT_RESPONSE message from the Network Emulator. 
 *                   It initializes the routing table with the bootstrap neighbor information in INIT_RESPONSE.  
 *                   Also sets up a route to itself (self-route) with next_hop as itself and cost as 0.
 */
void InitRoutingTbl (struct pkt_INIT_RESPONSE *InitResponse, int myID)
{
	int i = 0;
	for(i = 0; i < InitResponse->no_nbr; i++)
	{
		routingTable[i].dest_id = InitResponse->nbrcost[i].nbr; 
		routingTable[i].next_hop = InitResponse->nbrcost[i].nbr;
		routingTable[i].cost = InitResponse->nbrcost[i].cost;
	}
	routingTable[i].dest_id = myID; 
	routingTable[i].next_hop = myID;
	routingTable[i].cost = 0;

	NumRoutes = i + 1;
}



/* Routine Name    : UpdateRoutes
 * INPUT ARGUMENTS : 1. (struct pkt_RT_UPDATE *) - The Route Update message from one of the neighbors of the router.
 *                   2. int - The direct cost to the neighbor who sent the update. 
 *                   3. int - My router's id received from command line argument.
 * RETURN VALUE    : int - Return 1 : if the routing table has changed on running the function.
 *                         Return 0 : Otherwise.
 * USAGE           : This routine is called after receiving the route update from any neighbor. 
 *                   The routing table is then updated after running the distance vector protocol. 
 *                   It installs any new route received, that is previously unknown. For known routes, 
 *                   it finds the shortest path using current cost and received cost. 
 *                   It also implements the forced update and split horizon rules. My router's id
 *                   that is passed as argument may be useful in applying split horizon rule.
 */
int UpdateRoutes(struct pkt_RT_UPDATE *RecvdUpdatePacket, int costToNbr, int myID)
{
	int i,j,k, flag = 0, cost=0;
	
	if(myID != RecvdUpdatePacket->dest_id)
	{
		return flag;
	}
	
	for(i = 0; i < RecvdUpdatePacket->no_routes; i++)
	{
		k = 0;
		cost = (((RecvdUpdatePacket->route[i].cost + costToNbr) > INFINITY) ? INFINITY : (RecvdUpdatePacket->route[i].cost + costToNbr));
		for(j = 0; j < NumRoutes; j++)
		{
			if (RecvdUpdatePacket->route[i].dest_id == routingTable[j].dest_id)
			{	
				k = 1;
				// Forced Update
				if (RecvdUpdatePacket->sender_id == routingTable[j].next_hop)
				{
					if (routingTable[j].cost != cost)
					{
						routingTable[j].cost = cost;
						flag = 1;
					}
				}

				// Split Horizon and Low Cost
				else if (myID != RecvdUpdatePacket->route[i].next_hop && routingTable[j].cost > cost)
				{
					routingTable[j].cost = cost;
					routingTable[j].next_hop = RecvdUpdatePacket->sender_id;
					flag = 1;
				}
			}
		}
		// Update routing table with unknown routes
		if(k == 0)
		{
			routingTable[j].dest_id = RecvdUpdatePacket->route[i].dest_id; 
			routingTable[j].next_hop = RecvdUpdatePacket->sender_id;//RecvdUpdatePacket->route[i].next_hop;
			routingTable[j].cost = cost;
			NumRoutes += 1;
			flag = 1;
		}
	}
	return flag;
}

/* Routine Name    : ConvertTabletoPkt
 * INPUT ARGUMENTS : 1. (struct pkt_RT_UPDATE *) - An empty pkt_RT_UPDATE structure
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine fills the routing table into the empty struct pkt_RT_UPDATE. 
 *                   My router's id  is copied to the sender_id in pkt_RT_UPDATE. 
 *                   Note that the dest_id is not filled in this function. When this update message 
 *                   is sent to all neighbors of the router, the dest_id is filled.
 */
void ConvertTabletoPkt(struct pkt_RT_UPDATE *UpdatePacketToSend, int myID)
{
	UpdatePacketToSend->sender_id = myID;
	UpdatePacketToSend->no_routes = NumRoutes;
	int i;
	for (i=0; i< NumRoutes; i++)
	{
		UpdatePacketToSend->route[i].dest_id = routingTable[i].dest_id;
		UpdatePacketToSend->route[i].next_hop = routingTable[i].next_hop;
		UpdatePacketToSend->route[i].cost = routingTable[i].cost;
	}
}



/* Routine Name    : PrintRoutes
 * INPUT ARGUMENTS : 1. (FILE *) - Pointer to the log file created in router.c, with a filename that uses MyRouter's id.
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine prints the routing table to the log file 
 *                   according to the format and rules specified in the Handout.
 */
void PrintRoutes (FILE* Logfile, int myID)
{
	fprintf(Logfile, "Routing Table:\n");
	int i;

	fprintf(Logfile, "R%d -> R%d: R%d, %d\n", myID, myID, myID, 0);

	for (i=0; i< NumRoutes; i++)
	{	
		if(routingTable[i].dest_id != myID)
		{
			fprintf(Logfile, "R%d -> R%d: R%d, %d\n", myID, routingTable[i].dest_id, routingTable[i].next_hop, routingTable[i].cost);
		}
	}
}


/* Routine Name    : UninstallRoutesOnNbrDeath
 * INPUT ARGUMENTS : 1. int - The id of the inactive neighbor 
 *                   (one who didn't send Route Update for FAILURE_DETECTION seconds).
 *                   
 * RETURN VALUE    : void
 * USAGE           : This function is invoked when a nbr is found to be dead. The function checks all routes that
 *                   use this nbr as next hop, and changes the cost to INFINITY.
 */
void UninstallRoutesOnNbrDeath(int DeadNbr)
{
	int i;
	for (i=0; i< NumRoutes; i++)
	{
		if( DeadNbr == routingTable[i].next_hop)
		{
			routingTable[i].cost = INFINITY;
		}
	}
}


/* Variable      : struct route_entry routingTable[MAX_ROUTERS]
 * Variable Type : Array of type (struct route_entry)
 * USAGE         : Define as a Global Variable in routingtable.c.
 *                 The routingTable will be used by all the functions in routingtable.c.
 *                 #include ne.h in routingtable.c for definitions of struct route_entry and MAX_ROUTERS.
 */


/* Variable      : int NumRoutes
 * Variable Type : Integer
 * USAGE         : Define as a Global Variable in routingtable.c.
 *                 This variable holds the number of routes present in the routing table.
 *                 It is initialized on receiving INIT_RESPONSE from Network Emulator
 *                 and is updated in the UpdateRoutes() function, whenever the routingTable changes. 
 */