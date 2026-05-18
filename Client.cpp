#define NOMINMAX

#pragma comment(lib, "Ws2_32.lib")

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


// endpoints
// static class
struct Endpoint
{
	// attributes
	static inline sockaddr_in sockaddrClient // client sockaddr
	{
		.sin_family = AF_INET ,
		.sin_addr =
		{
			.S_un =
			{
				.S_addr = inet_addr("192.168.0.159") //- change on each machine
			}
		}
	};

	static inline sockaddr_in sockaddrServer // server sockaddr
	{
		.sin_family = AF_INET
	};
};


// sock startup
class sock
{
	public:
		// attributes
		SOCKET sockfd;


		// constructor
		sock() // default
		{
			sockfd = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
			if (sockfd == INVALID_SOCKET)
			{
				errHandle(WSAGetLastError() , "socket(AF_INET , SOCK_STREAM , IPPROTO_TCP)");
			}
			std::cout << "Client running" << std::endl;
		}

		explicit sock(SOCKET sockListening) : // only used for accept()
			sockfd(sockListening)
			{
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
			std::cout << "Socket bound to " << sockAddr.sin_addr << ":" << ntohs(sockAddr.sin_port) << std::endl;


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

		SOCKET sockAccept(sockaddr_in &sockAddr) const // accept
		{
			int sockaddrLen = sizeof(sockAddr);
			SOCKET sockClient = accept(sockfd , reinterpret_cast<sockaddr*>(&sockAddr) , &sockaddrLen);
			if (sockClient == INVALID_SOCKET)
			{
				errHandle(WSAGetLastError() , "accept(sockfd , reinterpret_cast<sockaddr*>(&sockAddr) , NULL)");
			}
			std::cout << "Client socket accepted @ " << sockAddr.sin_addr << std::endl << std::endl;


			return sockClient;
		}

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
		uint32_t _header = 0; // only used for reads, use header() for writes
		std::string body; // message

		// quasi attribute
		inline uint32_t header() const // 4-byte message length. only used for writes, use _header for reads
		{
			return static_cast<uint32_t>(body.size());
		}


		// constructor
		Packet() = default; // used for reads

		explicit Packet(const std::string &msg) : // used for writes
			body(msg)
			{
			}


		// methods
		std::string serialise() const // formatter, preps message for send
		{
			std::string payload;
			payload.reserve(sizeof(uint32_t) + header());

			uint32_t nboHeader = htonl(header());

			payload.append(reinterpret_cast<const char*>(&nboHeader) , sizeof(nboHeader)).append(body);


			return payload;
		}

		std::string deserialise() // deformatter, extracts message
		{
		}
};


// protocol handling
class Protocol
{
	public:
		// attributes
		Packet packet;


		// constructor
		Protocol() = default; // used for reads

		explicit Protocol(const std::string &msg) : // used for writes
			packet(msg)
			{
			}


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
};


// user interactions
// static class
class Interactions
{
	public:
		// methods
		static void inputIP() // input server ip to connect to
		{
			std::string ip;
			while (true)
			{
				std::cout << "Enter the server's IP address: ";
				std::getline(std::cin , ip);
				if (inet_pton(AF_INET , ip.data() , &Endpoint::sockaddrServer.sin_addr) != 1)
				{
					std::cout << "Invalid input. Enter a valid IPv4 address" << std::endl;
					continue;
				}
				break;
			}


			return;
		}

		static void inputPort() // input port to bind to
		{
			int port;
			while (true)
			{
				std::string input;
				std::cout << "Enter the server's port: ";
				std::getline(std::cin , input);
				try
				{
					size_t size;
					port = std::stoi(input , &size);
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

			Endpoint::sockaddrServer.sin_port = htons(port);
			Endpoint::sockaddrClient.sin_port = htons(port);


			return;
		}

		static std::string inputMsg() // input message
		{
			std::string msg;
			while (true)
			{
				std::cout << ">";
				std::getline(std::cin , msg);
				if (msg.empty())
				{
					continue;
				}
				break;
			}


			return msg;
		}
};

int main()
{
	sock sockClient; // socket()-> | waiting for bind() listen() accept()<-

	Interactions::inputIP();
	Interactions::inputPort();

	sockClient.sockConnect(Endpoint::sockaddrServer);

	Protocol sendPacket(Interactions::inputMsg());
	sendPacket.packetOut(sockClient.sockfd);


	return 0;
}