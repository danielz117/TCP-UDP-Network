#include <string>
#include <thread>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <inttypes.h>
#include <stdio.h>
#include <cstring>
#include <fstream>
#include <arpa/inet.h>
#include <vector>
#include <set>
#include <map>


using namespace std;

#pragma pack(1)
struct header {
	uint32_t sequence_num;
	uint32_t ack_num;
	uint16_t connection_id;
	uint16_t flags;
};

struct packet {
	header packet_header;
	char data[512];
};

#pragma pack()



struct connection {
	uint16_t connection_id;
	int ssthresh;
	int cwnd;
	bool gotFin = false;
	bool open;
	uint32_t expectedSeq;
	map<int, int> bufferedPackets;
	char place = 'a';
	char* receiverWindow = &place;
	std::chrono::steady_clock::time_point time;
};

string optionalPrint(int ack, int syn, int fin, int dup) {
	string output;
	if (ack && syn) {
		output = "ACK SYN";
	}
	else if (ack && fin) {
		output = "ACK FIN";
	}
	else if (ack && dup) {
		output = "ACK DUP";
	}
	else if (ack && syn && dup) {
		output = "ACK SYN DUP";
	}
	else if (syn && dup) {
		output = "SYN DUP";
	}
	else if (ack) {
		output = "ACK";
	}
	else if (syn) {
		output = "SYN";
	}
	else if (fin) {
		output = "FIN";
	}
	else if (dup) {
		output = "DUP";
	}
	return output;
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		cerr << "ERROR: Usage: " << argv[0] << " <PORT> " << endl;
		exit(1);
	}
	
	string dir = argv[2];

	int serverSockFd = socket(AF_INET, SOCK_DGRAM, 0);
	struct addrinfo serveraddr;
	memset(&serveraddr, '\0', sizeof(serveraddr));
	serveraddr.ai_family = AF_INET;
	serveraddr.ai_socktype = SOCK_DGRAM;
	serveraddr.ai_flags = AI_PASSIVE;

	fd_set rset;
	//auto timeout = timeval();
	//timeout.tv_sec = 10;
	struct addrinfo* myAddrInfo;
	int ret;
	if ((ret = getaddrinfo(NULL, argv[1], &serveraddr, &myAddrInfo)) != 0) {
		cerr << "ERROR: address" << endl;
		exit(1);
	}

	if (bind(serverSockFd, myAddrInfo->ai_addr, myAddrInfo->ai_addrlen) == -1) {
		cerr << "ERROR: cannot bind socket" << endl;
		exit(1);
	}
	FD_ZERO(&rset);

	vector<connection> allClients;
	set<int> abortedConnections;
	int connectionID = 0;
	while (1) {
		FD_SET(serverSockFd, &rset);
		uint8_t received_buf[524];
		struct sockaddr addr;
		socklen_t addr_len = sizeof(struct sockaddr);

		/*int nselected = select(serverSockFd + 1, &rset, NULL, NULL, NULL); //dont need select (use multithreading or event driven) should be able to process packets as they come in
		//one thread to process the packets from the socket/queue it and another to receive the data
		if (nselected == -1) {
			cerr << "Select error" << endl;
			exit(1);
		}
		if (!FD_ISSET(serverSockFd, &rset)) {
			continue;
		}*/
		// Non-blocking attempt 		
		/*
		timeval rto;
		rto.tv_sec = 0;
		rto.tv_usec = 500000;
		fd_set fs;
		FD_ZERO(&fs);
		FD_SET(socket, &fs);
		int received = select(serverSockFd+1, &fs, NULL, NULL, &rto);
		*/

		ssize_t length = recvfrom(serverSockFd, received_buf, sizeof(received_buf), 0, &addr, &addr_len);
		//cerr << "DATA received " << length << " bytes from : " <<
		//	inet_ntoa(((struct sockaddr_in*)&addr)->sin_addr) << endl;
		packet* received_packet = reinterpret_cast<packet*>(received_buf);
		header received_head = received_packet->packet_header;
		uint32_t received_seq = ntohl(received_head.sequence_num);
		uint32_t received_ack = ntohl(received_head.ack_num);
		uint16_t received_conn = ntohs(received_head.connection_id);

		if (abortedConnections.find(received_conn-1) != abortedConnections.end()) {
			continue;
		}

		uint16_t received_flag = ntohs(received_head.flags);
		int FINflag = (received_flag & (1 << 0)) > 0;
		int SYNflag = (received_flag & (1 << 1)) > 0;
		int ACKflag = (received_flag & (1 << 2)) > 0;
		char *data = received_packet->data;

		cout << "RECV " << received_seq << " " << received_ack << " " << received_conn <<
			" " << optionalPrint(ACKflag, SYNflag, FINflag, 0) << endl;

		packet send_packet;
		header send_head;
		uint32_t send_seq = 4321;
		uint32_t send_ack = received_seq + 1;
		uint16_t send_conn = connectionID;
		int sendFINflag;
		int sendSYNflag;
		int sendACKflag;

		if (SYNflag == 1) {
			connectionID++;
			connection currClient;
			currClient.connection_id = connectionID;
			currClient.open = true;
			allClients.push_back(currClient);
			send_seq = 4321;
			send_ack = (received_seq + 1) % 102401;
			send_conn = connectionID;
			sendFINflag = 0;
			sendSYNflag = 1;
			sendACKflag = 1;

			send_head.sequence_num = htonl(send_seq);
			send_head.ack_num = htonl(send_ack);
			send_head.connection_id = htons(send_conn);

			send_head.flags = 0;
			send_head.flags |= 1UL << 1;
			send_head.flags |= 1UL << 2;
			send_head.flags = htons(send_head.flags);
		}
		else if (FINflag == 1) {
			send_seq = 4322;
			send_ack = (received_seq + 1)% 102401;
			send_conn = received_conn;
			sendFINflag = 1;
			sendSYNflag = 0;
			sendACKflag = 1;

			send_head.sequence_num = htonl(send_seq);
			send_head.ack_num = htonl(send_ack);
			send_head.connection_id = htons(send_conn);

			send_head.flags = 0;
			send_head.flags |= 1UL << 2;
			send_head.flags |= 1UL << 0;
			send_head.flags = htons(send_head.flags);
			
		}
		else if (length == 12) {
			continue;
		}
		else {
			send_seq = 4322;
			send_ack = (received_seq + (length - 12)) % 102401;
			send_conn = received_conn;
			sendFINflag = 0;
			sendSYNflag = 0;
			sendACKflag = 1;	

			connection currClient = allClients[received_conn - 1];
			//cerr << "Expected Seq: " << currClient.expectedSeq << endl;
			if (received_seq != currClient.expectedSeq) {
				send_ack = currClient.expectedSeq;
				send_head.sequence_num = htonl(send_seq);
				send_head.ack_num = htonl(send_ack);
				send_head.connection_id = htons(send_conn);

				send_head.flags = 0;
				send_head.flags |= 1UL << 2;
				send_head.flags = htons(send_head.flags);
				send_packet.packet_header = send_head;
				length = sendto(serverSockFd, &send_packet, sizeof(send_packet), 0, &addr, addr_len);
				cout << "SEND " << send_seq << " " << send_ack << " " << send_conn << " " << optionalPrint(sendACKflag, sendSYNflag, sendFINflag, 0) << endl;
				continue;
			}
			else {
				connection currClient = allClients[received_conn - 1];
				currClient.expectedSeq = received_seq + (length - 12);
			}

			/*
			if (received_conn > allClients.size()) {
				cout << "DROP " << received_seq << " " << received_ack << " " << received_conn <<
					" " << optionalPrint(ACKflag, SYNflag, FINflag, 0) << endl;
				continue;
			}

			if (received_ack == 4323 && allClients[send_conn-1].gotFin) {
				allClients[send_conn - 1].open = false;
				continue;
			}
			else {
				cout << "DROP " << received_seq << " " << received_ack << " " << received_conn <<
					" " << optionalPrint(ACKflag, SYNflag, FINflag, 0) << endl;
			}*/

			send_head.sequence_num = htonl(send_seq);
			send_head.ack_num = htonl(send_ack);
			send_head.connection_id = htons(send_conn);
			
			send_head.flags = 0;
			send_head.flags |= 1UL << 2;
			send_head.flags = htons(send_head.flags);
		}

		std::chrono::steady_clock::time_point start_time;
		start_time = std::chrono::steady_clock::now();
		allClients[send_conn - 1].time = start_time;
		allClients[send_conn - 1].expectedSeq = send_ack;

		string path = dir + "/" + to_string(send_conn) + ".file";
		ofstream file;
		file.open(path, ios_base::app);
		if (SYNflag == 0 && FINflag == 0) {
			file.write((char*)data, length-12);

		}

		file.close();

		//write a class for every connection. use select (waits on the input socket and returns when we get a packet or timeout) on one socket
		send_packet.packet_header = send_head;
		length = sendto(serverSockFd, &send_packet, sizeof(send_packet), 0, &addr, addr_len);
		cout << "SEND " << send_seq << " " << send_ack << " " << send_conn << " " << optionalPrint(sendACKflag, sendSYNflag, sendFINflag, 0) << endl;

		/*for (int i = 0; i < allClients.size(); i++) {
			if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - allClients[i].time).count() > 10) {
				abortedConnections.insert(i);
				string pathAbort = dir + "/" + to_string(i + 1) + ".file";
				ofstream file;
				file.open(pathAbort);
				file.write("Error", sizeof("Error"));
				file.close();
			}
		}*/

		//received = select(serverSockFd + 1, &inSet, NULL, NULL, &timeout);		

		//length = sendto(serverSockFd, "ACK", strlen("ACK"), MSG_CONFIRM, &addr, addr_len);
		//cout << length << " bytes ACK sent" << endl;
	}
}



