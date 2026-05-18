#define NOMINMAX

#pragma comment(lib, "WS2_32.lib")

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <WinSock2.h>
#include <WS2tcpip.h>


// error handling
static_assert(sizeof(uint32_t) == 4 , "sizeof(uint32_t) != 4\nyour compiler sure has an interesting definition of 32 bits...");

[[noreturn]] void errHandle(int errNo , const char* errLocation)
{
	LPWSTR errMsg;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK , NULL , errNo , NULL , reinterpret_cast<LPWSTR>(&errMsg) , NULL , NULL);

	std::wcerr << "=== ERROR --- {" << errLocation << "} FAILED --- " << errNo << ": " << errMsg << "===" << std::endl;
	std::cerr << "=== PROGRAM TERMINATED {" << errNo << "} ===" << std::endl;

	LocalFree(errMsg);
	exit(errNo);
}


// overloads operator<< to display IP address in correct byte order
inline std::ostream &operator<<(std::ostream &cout , const in_addr &IPv4)
{
	return cout << inet_ntoa(IPv4);
}

//x // overloads operator<< to display port in correct byte order
//x inline std::ostream &operator<<(std::ostream &cout , const USHORT &port) //! can't fucking do this!
//x {
//x 	return cout << ntohs(port);
//x }


// winsock startup
class wsa
{
	public:
		// attributes
		WSADATA wsaData;
		int wsaStartup;

		// constructor
		wsa()
		{
			wsaStartup = WSAStartup(MAKEWORD(2 , 2) , &wsaData);
			if (wsaStartup)
			{
				errHandle(wsaStartup , "WSAStartup(MAKEWORD(2 , 2) , &wsaData)");
			}
			std::cout << wsaData.szDescription << " status: " << wsaData.szSystemStatus << std::endl;
		}

		// destructor
		~wsa()
		{
			if (WSACleanup() == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "WSACleanup()");
			}
		}
};

wsa wsaStartup;


// sock startup
class sock
{
	public:
		// attributes
		SOCKET sockfd;


		// constructor
		sock()
		{
			sockfd = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
			if (sockfd == INVALID_SOCKET)
			{
				errHandle(WSAGetLastError() , "socket(AF_INET , SOCK_STREAM , IPPROTO_TCP)");
			}
		}

		// destructor
		~sock()
		{
			if (closesocket(sockfd) == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "closesocket(sockfd)");
			}
		}

		// methods
		void sockConnect(const sockaddr_in &sockAddr) const // connect
		{
			if (connect(sockfd , reinterpret_cast<const sockaddr*>(&sockAddr) , sizeof(sockAddr)) == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "connect(sockfd , reinterpret_cast<const sockaddr*>(&sockAddr) , sizeof(sockAddr))");
			}
			std::cout << "Connected to server socket @ " << sockAddr.sin_addr << ":" << ntohs(sockAddr.sin_port) << std::endl;

			return;
		}
};


// packet handling
class Packet
{
	public:
		// attributes
		const std::string body; // message

		// quasi attribute
		inline uint32_t header() const // 4-byte message length
		{
			return static_cast<uint32_t>(body.size());
		}


		// constructor
		Packet() = delete; // forces population at initialisation

		explicit Packet(const std::string &msg) :
			body(msg)
			{
			}


		// methods
		std::string serialise() const // formatter
		{
			std::string payload;
			payload.reserve(sizeof(uint32_t) + header());

			uint32_t nboHeader = htonl(header());

			payload.append(reinterpret_cast<const char*>(&nboHeader) , sizeof(nboHeader)).append(body);

			return payload;
		}

		//- deserialise
};


// protocol handling
class Protocol
{
	public:
		// attributes
		Packet packet;


		// constructor
		Protocol() = delete; // forces population at initialisation

		explicit Protocol(const std::string &msg) :
			packet(msg)
			{
			}


		// methods
		void packetOut(SOCKET sock) const // sends packet
		{
			std::string payload = packet.serialise();

			int bytesRemaining = payload.size();
			int bytesSentTotal = 0;
			while (bytesRemaining > 0)
			{
				int bytesSent = send(sock , payload.data() + bytesSentTotal , bytesRemaining , NULL);
				if (bytesSent == SOCKET_ERROR)
				{
					errHandle(WSAGetLastError() , "send(sock , payload.data() + bytesSentTotal , bytesRemaining , NULL)");
				}
				if (bytesSent == 0)
				{
					break;
				}
				bytesSentTotal += bytesSent;
				bytesRemaining -= bytesSent;
			}

			return;
		}

		//- packetIn
};


int main()
{
	sock sockClient;
	std::cout << "Client running" << std::endl;

	// server sock struct
	sockaddr_in sockaddrServer
	{
		.sin_family = AF_INET
	};


	std::string ip;
	while (true)
	{
		std::cout << "Enter the server's IP address: ";
		std::getline(std::cin , ip);
		if (ip.empty() || inet_pton(AF_INET, ip.data(), &sockaddrServer.sin_addr) != 1)
		{
			std::cout << "Invalid input. Enter a valid IPv4 address" << std::endl;
			continue;
		}
		break;
	}

	int port;
	while (true)
	{
		std::string input;
		std::cout << "Enter the server's port: ";
		std::getline(std::cin , input);
		try
		{
			size_t size;
			port = std::stoi(input, &size);
			if (size != input.size())
			{
				throw std::invalid_argument("stoi");
			}
			if (port < 1024 || port > 65535)
			{
				throw std::range_error("stoi");
			}
		}
		catch (...)
		{
			std::cout << "Invalid input. Enter a valid TCP port between 1024 and 65535" << std::endl;
			continue;
		}
		break;
	}
	sockaddrServer.sin_port = htons(port);


	sockClient.sockConnect(sockaddrServer);

	Protocol sendPacket("1234567890");
	sendPacket.packetOut(sockClient.sockfd);


	return 0;
}