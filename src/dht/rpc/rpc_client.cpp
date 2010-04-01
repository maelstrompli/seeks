/**
 * This is the p2p messaging component of the Seeks project,
 * a collaborative websearch overlay network.
 *
 * Copyright (C) 2010  Emmanuel Benazera, juban@free.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "rpc_client.h"
#include "DHTNode.h"
#include "spsockets.h"
#include "errlog.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <pthread.h>
#include <iostream>

using sp::spsockets;
using sp::errlog;

namespace dht
{
   rpc_client::rpc_client()
     {
     }
   
   rpc_client::~rpc_client()
     {
     }

   void rpc_client::do_rpc_call_static(rpc_call_args *args)
     {
	args->_err = args->_client->do_rpc_call(args->_server_na,args->_msg,
						args->_need_response,args->_response);
     }
   
   dht_err rpc_client::do_rpc_call_threaded(const NetAddress &server_na,
					    const std::string &msg,
					    const bool &need_response,
					    std::string &response)
     {
	rpc_call_args args(this,server_na,msg,need_response,response);
	
	pthread_t rpc_call_thread;
	int err = pthread_create(&rpc_call_thread,NULL, //joinable
				 (void * (*)(void *))&rpc_client::do_rpc_call_static,&args);
	
	// join.
	pthread_join(rpc_call_thread,NULL);
     
	return args._err;
     }
      
   dht_err rpc_client::do_rpc_call(const NetAddress &server_na,
				   const std::string &msg,
				   const bool &need_response,
				   std::string &response)
     {
	// create socket.
	int udp_sock = socket(AF_INET,SOCK_DGRAM,0);
	if (udp_sock < 0)
	  {
	     // debug
	     std::cout << "Error creating rpc_client socket\n";
	     // debug
	     
	     errlog::log_error(LOG_LEVEL_INFO,"Error creating rpc_client socket");
	     throw rpc_client_socket_error_exception();
	  }
	
	struct sockaddr_in server;
	server.sin_family = AF_INET; // beware, should do AF_INET6 also.
	struct hostent *hp = gethostbyname(server_na.getNetAddress().c_str());
	if (hp == 0)
	  {
	     //debug
	     std::cout << "Unknown host for rpc_client " << server_na.getNetAddress() << std::endl;
	     //debug
	     
	     errlog::log_error(LOG_LEVEL_INFO,"Unknown host for rpc_client %s", server_na.getNetAddress().c_str());
	     throw rpc_client_host_error_exception(server_na.getNetAddress());
	  }
		
	bcopy((char*)hp->h_addr,(char*)&server.sin_addr,hp->h_length);
	server.sin_port = htons(server_na.getPort());
	int length = sizeof(struct sockaddr_in);

	//debug
	std::cout << "rpc_client: sending msg...\n";
	//debug
	
	// send the message.
	int n = sendto(udp_sock,msg.c_str(),strlen(msg.c_str()),0,(struct sockaddr*)&server,length);
	if (n<0)
	  {
	     response.clear();
	     errlog::log_error(LOG_LEVEL_INFO,"Error sending rpc_client msg");
	     throw rpc_client_sending_error_exception();
	  }
		
	// receive message, if necessary.
	// TODO: timeout + exception.
	if (!need_response)
	  return DHT_ERR_OK;	
	
	// TODO: non blocking on (single) response.
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(udp_sock,&rfds);
	timeval timeout;
	timeout.tv_sec = DHTNode::_dht_config->_l1_client_timeout;
	timeout.tv_usec = 0;
	
	int m = select((int)udp_sock+1,&rfds,NULL,NULL,&timeout);
	
	if (m == 0) // no bits received before the communication timed out.
	  {
	     errlog::log_error(LOG_LEVEL_ERROR, "Didn't receive response data in time to layer 1 call");
	     response = "";
	     return DHT_ERR_OK;
	  }
	else if (m < 0)
	  {
	     errlog::log_error(LOG_LEVEL_ERROR, "select() failed!: %E");
	     return DHT_ERR_NETWORK;
	  }
		  
	size_t buflen = 1024;
	char buf[buflen];
	struct sockaddr_in from;
	n = recvfrom(udp_sock,buf,buflen,0,(struct sockaddr*)&from,(socklen_t*)&length);
	if (n<0)
	  {
	     response.clear();
	     errlog::log_error(LOG_LEVEL_INFO,"Error in response to rpc_client msg");
	     throw rpc_client_reception_error_exception();
	  }
	
	response = std::string(buf);
	
	return DHT_ERR_OK;
     }
   
} /* end of namespace. */