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
#include <chrono>
#include <vector>

#define ACK 4
#define SYN 2
#define FIN 1
#define SYN_ACK 6
#define ACK_FIN 5

#define MIN_WINDOW  512;   // bytes
#define MAX_WINDOW  51200;   // bytes
#define TIMEOUT 500;   // milliseconds

using namespace std;

unsigned int seq_num = 12345;
unsigned int ack_num = 0;
unsigned int con_num = 0;

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

void optionalPrint(int flag, int dupe) {
    if (flag == ACK) {
        cout << " ACK";
    }
    else if (flag == SYN) {
        cout << " SYN";
    }
    else if (flag == FIN) {
        cout << " FIN";
    }
    else if (flag == SYN_ACK) {
        cout << " SYN ACK";
    }
    else if (flag == ACK_FIN) {
        cout << " ACK FIN";
    }

    if (dupe) {
        cout << " DUP" << endl;
    }
    else {
        cout << endl;
    }
}

class CWND {
    private:
       
        int ssthresh; 
        int cwnd;
        vector<packet> packets;
        int num_packets;
 
    public:
      
        CWND() {
            ssthresh = 10000;
            cwnd = MIN_WINDOW;
            num_packets = 0;
        }
        
        int get_thresh() {
            return ssthresh;
        }
        
        int get_cwnd() {
            return cwnd;
        }

        int retransmit(int sockfd, struct sockaddr_in server_addr) {
            int sent; 
            if (sent = sendto(sockfd, &packets.front(), sizeof(packets.front()), 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) > 0) {
                cout << "SEND " << ntohl(packets.front().packet_header.sequence_num) << " " << ntohl(packets.front().packet_header.ack_num) << " " << ntohs(packets.front().packet_header.connection_id);
                cout << " " << cwnd << " " << ssthresh << " DUP" << endl;
            }
            return sent;
        }

        int get_num_packets() {
            return num_packets;
        }
        
        bool win_avail() {
            // check if there is room to send a packet
            if (num_packets * 512 < cwnd) {
                return true;
            }
            return false;
        }

        void packet_sent(packet new_packet) {    // ill do error codes later?
            // add the packet that was just sent to the packets vector
            packets.push_back(new_packet);
            num_packets += 1;
        }

        int ack_received(uint32_t ack_number) {
            // check if the ack num corresponds to one of the recent packets sent out
            if (packets.empty()) {
                return 1;
            }
            while (!packets.empty() 
                && (ntohl(packets.front().packet_header.sequence_num) % 102401) <= ((ack_number) % 102401)) {
                // if the sequence number in the window is smaller than the ack number received,
                // then remove that element (cumulative ack)
                packets.erase(packets.begin());
                num_packets -= 1;


                // adjust cwnd and phase and ssthresh
                if (cwnd < ssthresh) {    // slow start
                    // cout << "slow start" << endl;
                    cwnd += 512;
                }
                else if (cwnd >= ssthresh) {
                    // cout << "congestion avoidance" << endl;
                    cwnd += (512 * 512) / cwnd;
                }
                // cout << "CWND after " << cwnd << endl;
            }

            return 0;
        }
        
        int timeout() {
            // if a timeout occurs
            ssthresh = cwnd / 2;
            cwnd = MIN_WINDOW;
            return 0;   // idk why this wouldn't work but okay
        }

};

int main(int argc, char** argv) {

    // handle arguments
    if (argc != 4) {
        cerr << "ERROR: Usage: ./client <HOSTNAME-OR-IP> <PORT> <FILENAME>" << endl;
        exit(1);
    }

    char* ip = argv[1];
    long dest_ip = 0;
    if (strcmp(argv[1], "localhost") == 0) {
        dest_ip = 0x7f000001;
    }
    else {
        int i = 0, c = 0;
        while (ip[i] != 0) {
            if (ip[i] == '.') {
                dest_ip += c;
                dest_ip *= 256;
                c = 0;
            }
            else if (ip[i] < '0' || ip[i] > '9') {
                cerr << "ERROR: Invalid IP address" << endl;
                exit(1);
            }
            else {
                c *= 10;
                c += ip[i] - '0';
            }
            i++;
        }

        dest_ip += c;
    }

    string port_num = argv[2];
    int dest_port = stoi(argv[2]);
    if (dest_port < 0 || dest_port > 65535) {
        cerr << "ERROR: Invalid port number" << endl;
        exit(1);
    }

    char* filename = argv[3];

    // set up socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dest_port);
    server_addr.sin_addr.s_addr = htonl(dest_ip);
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));
    struct sockaddr* dest_addr = (struct sockaddr*) &server_addr;

    // create and send syn packet to initiate handshake
    packet syn_packet;
    syn_packet.packet_header.sequence_num = htonl(seq_num);
    syn_packet.packet_header.ack_num = htonl(ack_num);
    syn_packet.packet_header.connection_id = htons(con_num);
    syn_packet.packet_header.flags = htons(SYN);
    if (sendto(sockfd, &syn_packet, sizeof(struct packet), 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
        cerr << "ERROR: Cannot send SYN to server" << endl;
        exit(1);
    }
    else {
	    cout << "SEND " << seq_num << " " << ack_num << " " << con_num  << " 512 10000" << " SYN" << endl; 
    }

    // receive synack 
    socklen_t addr_len = sizeof(server_addr);
    uint8_t received_buf[524];
    ssize_t length = recvfrom(sockfd, received_buf, 524, 0, (struct sockaddr*) &server_addr, &addr_len);

    header* received_head = reinterpret_cast<header*>(received_buf);
    uint32_t received_seq = ntohl(received_head->sequence_num);
    uint32_t received_ack = ntohl(received_head->ack_num);
    uint16_t received_conn = ntohs(received_head->connection_id);
    uint16_t received_flag = ntohs(received_head->flags);

    cout << "RECV " << received_seq << " " << received_ack << " " << received_conn  << " 512 10000 SYN ACK" << endl;

    // create congestion window
    CWND congestion_window;

    // send a file
    seq_num += 1;
    seq_num %= 102401;

    FILE *fd = fopen(filename, "r");
    if (fd == NULL) {
        cerr << "ERROR: Cannot open file" << endl;
        exit(1);
    }
    long file_size;
    fseek(fd, 0, SEEK_END);
    file_size = ftell(fd);
    rewind(fd);
    long bytes_read = 0;

    // if this is the first payload / ack message
    bool ack_msg = true;

    // send ack with payload
    seq_num = received_ack % 102401;
    ack_num = (received_seq + 1) % 102401;
    con_num = received_conn;

    bool send = true;

    // while we haven't read the entire file
    while(bytes_read < file_size) {
        // send packets until the window is full
        while (congestion_window.win_avail() && bytes_read < file_size && send) {
            packet file;
            file.packet_header.sequence_num = htonl(seq_num);
            file.packet_header.ack_num = htonl(ack_num);
            file.packet_header.connection_id = htons(con_num);
            if (ack_msg) {
                file.packet_header.flags = htons(ACK);
            }
            else {
                file.packet_header.flags = htons(0);
            }

            // check if we are reaching end of file or if we read 512 bytes
            int count;
            if(bytes_read + 512 <= file_size) {
                count = fread(&file.data, sizeof(char), 512, fd);
                bytes_read += count;
            }
            else {
                int remaining_bytes = file_size - bytes_read;
                count = fread(&file.data, sizeof(char), remaining_bytes, fd);
                bytes_read += count;
            }

            if (sendto(sockfd, &file, count + sizeof(struct header), 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
                cerr << "ERROR: Cannot send file to server" << endl;
                exit(1);
            }
            else {
                cout << "SEND " << seq_num << " " << ack_num << " " << con_num;
                cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
                if (ack_msg) {
                    optionalPrint(ACK, 0);
                }
                else {
                    cout << endl;
                }
            }
            congestion_window.packet_sent(file);
            seq_num += count;

            // update sequence number for overflow
            seq_num %= 102401;
            ack_msg = false;
        }

        send = false;

        // should receive until the window is empty
        if (!send && congestion_window.get_num_packets() > 0) {
            // create timer for when it is time to receive acks
            chrono::steady_clock::time_point ack_start, ack_recent, ack_curr;
            ack_start = chrono::steady_clock::now();
            ack_recent = chrono::steady_clock::now();
          
            while (congestion_window.get_num_packets() != 0) {
                fd_set rfd;
                FD_ZERO(&rfd);
                FD_SET(sockfd, &rfd);

                // use 0 seconds for this timeout
                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;

                // store value of select (found, timeout, or error)
                int slct = select(sockfd + 1, &rfd, NULL, NULL, &timeout);

                // if a fd is ready to be read
                if (slct == 1) {
                    // first receive the packet into a buffer
                    uint8_t ack_received_buf[524];
                    ssize_t ack_length = recvfrom(sockfd, ack_received_buf, 524, 0, (struct sockaddr*) &server_addr, &addr_len);
                    
                    // parse buffer header for sequence number and ack number
                    header* ack_received_head = reinterpret_cast<header*>(ack_received_buf);
                    uint32_t ack_received_ack = ntohl(ack_received_head->ack_num);
                    uint32_t ack_received_seq = ntohl(ack_received_head->sequence_num);
                    uint16_t ack_received_conn = ntohs(ack_received_head->connection_id);

                    cout << "RECV " << ack_received_seq << " " << ack_received_ack << " " << ack_received_conn;
                                cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh() << " ACK" << endl;

                    // update general ack_num
                    ack_num = 0;

                    // if acks are found, then update the congestion window
                    congestion_window.ack_received(ack_received_ack);

                    // every time a packet is received, reset the clock to the current time
                    // reset the clock back to 0
                    ack_recent = chrono::steady_clock::now();
                    ack_start = chrono::steady_clock::now();
                }
                // timeout
                else if (slct == 0) {

                    ack_curr = chrono::steady_clock::now();
                    // if we haven't received anything in over 10 seconds
                    if (chrono::duration_cast<chrono::seconds>(ack_curr - ack_start).count() >= 10) {
                        cerr << "ERROR: Nothing received for 10 seconds" << endl;
			close(sockfd);
			exit(1);
                    }
                    // if a packet times out
                    else if (chrono::duration_cast<chrono::nanoseconds>(ack_curr - ack_recent).count() >= 500000000) {
                        // retransmit the first packet
                        if (congestion_window.retransmit(sockfd, server_addr) < 0) {
                            cerr << "ERROR: Cannot send file to server" << endl;
                            exit(1);
                        }
                        congestion_window.timeout();
                        ack_recent = chrono::steady_clock::now();
                    }
                    // if neither of the above happen, just continue
                    else {
                        continue;
                    }
                }
                // if there is an error with select
                else if (slct == -1) {
                    cerr << "ERROR: Select cannot check fds" << endl;
                    exit(1);
                }
            }
        }
        send = true;
    }

    fclose(fd);

    // send fin
    int client_ack = 0;
    chrono::steady_clock::time_point start, recent, curr;

    ack_num = 0;
    packet fin_packet;
    uint8_t recv_packet[524];

    fin_packet.packet_header.sequence_num = htonl(seq_num);
    fin_packet.packet_header.ack_num = htonl(ack_num);
    fin_packet.packet_header.connection_id = htons(con_num);
    fin_packet.packet_header.flags = htons(FIN);

    // find a way to not hardcode the packet size
    start = chrono::steady_clock::now();
    recent = chrono::steady_clock::now();
    if (sendto(sockfd, &fin_packet, sizeof(struct header), 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
        cerr << "ERROR: Cannot send FIN to server" << endl;
        exit(1);
    }
    else {
        cout << "SEND " << seq_num << " " << ack_num << " " << con_num;
        cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
        optionalPrint(FIN, 0);
    }
    
    int done = 1;
    packet ack_packet;
    while (done) {
        // 10 second timer  (if the client doesn't get anything for 10 seconds, exit)
        curr = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(curr - start).count() >= 10) {
            cerr << "ERROR: nothing received for 10 seconds" << endl;
	    close(sockfd);
	    exit(1);
        }

        // retransmit after 0.5 second
        if (chrono::duration_cast<chrono::seconds>(curr - recent).count() >= 0.5) {
            sendto(sockfd, &fin_packet, sizeof(struct packet), 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
            recent = chrono::steady_clock::now();
            cout << "SEND " << seq_num << " " << ack_num << " " << con_num;
            cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
            optionalPrint(FIN, 1);
        }

        // receive      (wait here until we receive a response from the server to FIN)
        length = recvfrom(sockfd, recv_packet, 524, 0, (struct sockaddr*) &server_addr, &addr_len);
        header* recv_head = reinterpret_cast<header*>(recv_packet);
        uint32_t recv_seq = ntohl(recv_head->sequence_num);
        uint32_t recv_ack = ntohl(recv_head->ack_num);
        uint16_t recv_conn = ntohs(recv_head->connection_id);
        uint16_t recv_flag = ntohs(recv_head->flags);
		
	
        // if we received more than 0 bits
        if (length > 0) {
            cout << "RECV " << recv_seq << " " << recv_ack << " " << recv_conn;
            cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
            optionalPrint(recv_flag, 0);
            start = chrono::steady_clock::now();

            // if the packet's ack is 1 more than the last sequence number or if the packet's a FIN packet
            if (recv_ack == seq_num + 1 || recv_flag == FIN) {
                // if it was also an ACK or a FIN ACK
                if (recv_flag == ACK || recv_flag == ACK_FIN) {
                    // prep to send final ACK
                    ack_num = (recv_seq + 1) % 102401;
                    seq_num = recv_ack % 102401;
                    ack_packet.packet_header.sequence_num = htonl(seq_num);
                    ack_packet.packet_header.ack_num = htonl(ack_num);
                    ack_packet.packet_header.connection_id = htons(con_num);
                    ack_packet.packet_header.flags = htons(ACK);
                    sendto(sockfd, &ack_packet, sizeof(struct packet), 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
                    cout << "SEND " << seq_num << " " << ack_num << " " << con_num;
                    cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
                    optionalPrint(ACK, 0); 
                    start = chrono::steady_clock::now();
                    client_ack = 1;     // the client sent the ack
                }
                
                // while the ack is sent and the connection is still open
                while (client_ack) {
                    // start the timer of 2 seconds
                    curr = chrono::steady_clock::now();
                    if (chrono::duration_cast<chrono::seconds>(curr - start).count() >= 2) {
                        done = 0;
			close(sockfd);
                        break;
                    }

                    fd_set rfd;
                    FD_ZERO(&rfd);
                    FD_SET(sockfd, &rfd);

                    struct timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 0;

                    int slct = select(sockfd+1, &rfd, NULL, NULL, &timeout);
                    // if something was sent
                    if (slct == 1) {
                        if (length = recvfrom(sockfd, &recv_packet, 524, 0, (struct sockaddr*) &server_addr, &addr_len) != -1) {
                            recv_head = reinterpret_cast<header*>(recv_packet);
                            recv_seq = ntohl(recv_head->sequence_num);
                            recv_ack = ntohl(recv_head->ack_num);
                            recv_conn = ntohs(recv_head->connection_id);
                            recv_flag = ntohs(recv_head->flags);
                            cout << "RECV " << recv_seq << " " << recv_ack << " " << recv_conn;
                            cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
                            optionalPrint(recv_flag, 0);
                            
                            if (recv_flag == FIN) {
                                ack_num = (recv_seq + 1) % 102401;
                                ack_packet.packet_header.sequence_num = htonl(seq_num);
                                ack_packet.packet_header.ack_num = htonl(ack_num);
                                ack_packet.packet_header.connection_id = htons(con_num);
                                ack_packet.packet_header.flags = htons(ACK);
                                sendto(sockfd, &ack_packet, sizeof(struct packet), 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
                                cout << "SEND " << seq_num << " " << ack_num << " " << con_num;
                                cout << " " << congestion_window.get_cwnd() << " " << congestion_window.get_thresh();
                                optionalPrint(ACK, 0); 
                            }
                            else {
                                cout << "DROP " << recv_seq << " " << recv_ack << " " << recv_conn;
                                optionalPrint(recv_flag, 0);
                            }
                        }
                    }
                    // if nothing was sent, just continue
                    else if (slct == 0) {
                        continue;
                    }
                    // if there was an error, exit
                    else if (slct == -1) {
                        cerr << "ERROR: Select error" << endl;
                        exit(1);
                    }
                }
		
            }   
            else {
                cout << "DROP " << recv_seq << " " << recv_ack << " " << recv_conn;
                optionalPrint(recv_flag, 0);
            }
        }
    }

}
