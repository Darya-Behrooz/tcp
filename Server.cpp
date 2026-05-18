#pragma comment(lib, "Ws2_32.lib")

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <WinSock2.h>


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

//x overloads operator<< to display port in correct byte order
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
			std::cout << "Server running" << std::endl;
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
		void sockBind(const sockaddr_in &sockAddr) const // bind
		{
			if (bind(sockfd , reinterpret_cast<const sockaddr*>(&sockAddr) , sizeof(sockAddr)) == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "bind(sockfd , reinterpret_cast<const sockaddr*>(&sockAddr) , sizeof(sockAddr))");
			}
			std::cout << "Socket bound to port " << ntohs(sockAddr.sin_port) << std::endl;

			return;
		}

		void sockListen() const // listen
		{
			if (listen(sockfd , SOMAXCONN) == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "listen(sockfd , SOMAXCONN)");
			}
			std::cout << "Socket listening..." << std::endl;

			return;
		}

		SOCKET sockAccept(const sockaddr_in &sockAddr) const // accept
		{
			SOCKET sockClient = accept(sockfd , NULL , NULL);
			if (sockClient == INVALID_SOCKET)
			{
				errHandle(WSAGetLastError() , "accept(sockfd , NULL , NULL)");
			}
			std::cout << "Client socket accepted @ " << sockAddr.sin_addr << std::endl << std::endl;

			return sockClient;
		}
};


// packet handling
class Packet
{
	public:
		// attributes
		uint32_t _header = 0; // only used for reads, use header() for writes
		std::string body;

		// quasi attribute
		inline uint32_t header() const // 4-byte message length. only used for writes, use header for reads
		{
			return static_cast<uint32_t>(body.size());
		}


		// constructor
		//? Packet() = delete; // forces population at initialisation
		//? 
		//? explicit Packet(const std::string &msg) :
		//? 	body(msg)
		//? 	{
		//? 	}


		// methods
		std::string deserialise() // deformatter (extracts message)
		{
		}
};


// protocol handling
class Protocol
{
	public:
		// attributes
		Packet packet;


		//? // constructor
		//? Protocol() = delete; // forces population at instantiation
		//? 
		//? explicit Protocol(const std::string &msg) :
		//? 	packet(msg)
		//? 	{
		//? 	}


		// methods
		[[nodiscard]] std::string packetIn(SOCKET sock) // receives packet
		{
			int bytesReceivedHeader = 0;
			while (bytesReceivedHeader < sizeof(packet._header))
			{
				int bytesHeader = recv(sock , reinterpret_cast<char*>(&packet._header) + bytesReceivedHeader , sizeof(packet._header) - bytesReceivedHeader , NULL); // gets length
				if (bytesHeader == SOCKET_ERROR)
				{
					errHandle(WSAGetLastError() , "recv(sock , reinterpret_cast<char*>(&packet._header) + bytesReceivedHeader , sizeof(packet._header) - bytesReceivedHeader , NULL)");
				}
				if (bytesHeader == 0)
				{
					break;
				}
				bytesReceivedHeader += bytesHeader;
			}
			packet._header = ntohl(packet._header);

			packet.body.resize(packet._header);

			int bytesReceivedBody = 0;
			while (bytesReceivedBody < packet._header)
			{
				int bytesBody = recv(sock , packet.body.data() + bytesReceivedBody , packet._header - bytesReceivedBody , NULL); // reads message
				if (bytesBody == SOCKET_ERROR)
				{
					errHandle(WSAGetLastError() , "recv(sock , packet.body.data() + bytesReceivedBody , packet._header - bytesReceivedBody , NULL)");
				}
				if (bytesBody == 0)
				{
					break;
				}
				bytesReceivedBody += bytesBody;
			}

			return packet.body;
		}
};


int main()
{
	// server sock struct
	sockaddr_in sockaddrServer
	{
		.sin_family = AF_INET ,
		.sin_port = htons(42069) ,
		.sin_addr =
		{
			.S_un =
		{
			.S_addr = inet_addr("192.168.0.159")
	}
	}
	};

	// client sock struct
	sockaddr_in sockaddrClient
	{
		.sin_family = AF_INET ,
		.sin_port = htons(42069)
	};

	sock sockServer;
	sock sockClient;

	sockServer.sockBind(sockaddrServer);
	sockServer.sockListen();

	sockClient.sockfd = sockServer.sockAccept(sockaddrClient);


	Protocol recvPacket;
	std::cout << recvPacket.packetIn(sockClient.sockfd) << std::endl;


	return 0;
}

//- create both read and write functions on both sides
//- fix the constructors. they need to accept only strings for writes and only nothing for reads