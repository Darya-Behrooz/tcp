#define NOMINMAX

#pragma comment(lib, "Ws2_32.lib")

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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


// stdout/stderr mutex for overlapping console outputs
std::mutex consoleMutex;


// converts IPv4 address from string to in_addr
inline in_addr strIPv4(const std::string &str)
{
	in_addr IPv4;

	if (inet_pton(AF_INET , str.data() , &IPv4) != 1)
	{
		errHandle(WSAGetLastError() , "inet_pton(AF_INET , str.data() , &IPv4)");
	}


	return IPv4;
}

// converts IPv4 address from in_addr to string
inline std::string IPv4str(const in_addr &IPv4)
{
	char str[INET_ADDRSTRLEN];

	if (!inet_ntop(AF_INET , &IPv4 , str , INET_ADDRSTRLEN))
	{
		errHandle(WSAGetLastError() , "inet_ntop(AF_INET , &IPv4 , str , INET_ADDRSTRLEN)");
	}


	return str;
}


// overloads operator<< to print IPv4 address
inline std::ostream &operator<<(std::ostream &cout , const in_addr &IPv4)
{
	return cout << IPv4str(IPv4);
}

//x // overloads operator<< to print port
//x inline std::ostream &operator<<(std::ostream &cout , const USHORT &port) //! can't fucking do this!
//x {
//x 	return cout << ntohs(port);
//x }


// winsock startup
class wsa
{
	public:
		// constructor
		wsa()
		{
			WSADATA wsaData;
			int wsaErr = WSAStartup(MAKEWORD(2 , 2) , &wsaData);
			if (wsaErr)
			{
				errHandle(wsaErr , "WSAStartup(MAKEWORD(2 , 2) , &wsaData)");
			}
			std::cout << wsaData.szDescription << " status: " << wsaData.szSystemStatus << std::endl;
		}


		// rule of three
		wsa(const wsa&) = delete; // delete copy constructor
		wsa &operator=(const wsa&) = delete; // delete copy assignment operator


		// destructor
		~wsa() noexcept
		{
			if (WSACleanup() == SOCKET_ERROR)
			{
				errHandle(WSAGetLastError() , "WSACleanup()");
			}
		}
};

static wsa wsaStartup;


// endpoints
/*static*/struct Endpoint
{
	// attributes
	static inline in_addr ipLocal = strIPv4("192.168.0.159"); //- change on each server machine

	static inline sockaddr_in sockaddrServer // server sockaddr
	{
		.sin_family = AF_INET ,
		.sin_addr = strIPv4("0.0.0.0")
	};

	static inline sockaddr_in sockaddrClient // client sockaddr
	{
		.sin_family = AF_INET
	};
};


// user interactions
/*static*/class Interactions
{
	public:
		// attributes
		static inline std::string serverHandle; // server handle
		static inline std::string serverPrompt; // server prompt | handle@ip>
		static inline std::string clientHandle; // client handle
		static inline std::string clientPrompt; // client prompt | handle@ip>


		// methods
		static void inputPort() // input port to bind to
		{
			int port;
			while (true)
			{
				std::string input;
				std::cout << "Enter the TCP port to start the server on: ";
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

		static void inputHandle() // input nickname
		{
			std::cout << "Enter your handle or hit Enter to remain anonymous: ";
			std::getline(std::cin , serverHandle);
			if (serverHandle.empty())
			{
				serverHandle = "Server";
			}
			serverPrompt = serverHandle + "@" + IPv4str(Endpoint::ipLocal) + ">";


			return;
		}

		static std::string inputMsg() // input message
		{
			std::string msg;

			while (true)
			{
				{ // prevents prompt from overlapping
					std::lock_guard<std::mutex> consoleLock(consoleMutex); // mutex lock for overlapping console outputs

					std::cout << "\x1b[2K\r" << serverPrompt;
				}

				std::getline(std::cin , msg);
				if (!msg.empty())
				{
					break;
				}
			}


			return msg;
		}
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
			std::cout << "Server socket created" << std::endl;
		}

		explicit sock(SOCKET sockListening) : // only used for accept()
			sockfd(sockListening)
			{
			}


		// rule of three
		sock(const sock&) = delete; // delete copy constructor
		sock &operator=(const sock&) = delete; // delete copy assignment operator


		// destructor
		~sock() noexcept
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
};


// packet handling
class Packet
{
	public:
		// attributes
		std::string body; // message

		// quasi attribute
		inline uint32_t header() const noexcept // 4-byte message length
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
};


// protocol handling
class Protocol
{
	public:
		// attributes
		Packet packet;


		// methods
		[[nodiscard]] std::string packetIn(SOCKET sock) // receives packet
		{
			uint32_t header;
			std::string body;

			int bytesReceivedHeader = 0;
			while (bytesReceivedHeader < sizeof(header))
			{
				int bytesHeader = recv(sock , reinterpret_cast<char*>(&header) + bytesReceivedHeader , sizeof(header) - bytesReceivedHeader , NULL); // gets length
				if (bytesHeader == SOCKET_ERROR)
				{
					errHandle(WSAGetLastError() , "recv(sock , reinterpret_cast<char*>(&header) + bytesReceivedHeader , sizeof(header) - bytesReceivedHeader , NULL)");
				}
				if (bytesHeader == 0)
				{
					return "";
				}
				bytesReceivedHeader += bytesHeader;
			}
			header = ntohl(header);

			body.resize(header);

			int bytesReceivedBody = 0;
			while (bytesReceivedBody < header)
			{
				int bytesBody = recv(sock , body.data() + bytesReceivedBody , header - bytesReceivedBody , NULL); // reads message
				if (bytesBody == SOCKET_ERROR)
				{
					errHandle(WSAGetLastError() , "recv(sock , body.data() + bytesReceivedBody , header - bytesReceivedBody , NULL)");
				}
				if (bytesBody == 0)
				{
					return "";
				}
				bytesReceivedBody += bytesBody;
			}


			return body;
		}

		void packetInLoop(SOCKET sock) // recv loop for multithreading
		{
			Interactions::clientHandle = packetIn(sock);
			Interactions::clientPrompt = Interactions::clientHandle + "@" + IPv4str(Endpoint::sockaddrClient.sin_addr) + ">";

			while (true)
			{
				std::string payload = packetIn(sock);
				if (payload.empty())
				{
					std::cerr << "=== CONNECTION DISCONNECTED ===" << std::endl;
					break;
				}

				std::lock_guard<std::mutex> consoleLock(consoleMutex); // mutex lock for overlapping console outputs

				std::cout << "\x1b[2K\r" << Interactions::clientPrompt << payload << std::endl << Interactions::serverPrompt;
			}


			return;
		}

		void packetOut(SOCKET sock , const std::optional<std::string>&msg = std::nullopt) // sends packet
		{
			if (msg)
			{
				packet.body = *msg;
			}
			else
			{
				packet.body = Interactions::inputMsg();
			}
			std::string payload = packet.serialise();

			size_t bytesRemaining = payload.size();
			size_t bytesSentTotal = 0;
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

		void packetOutLoop(SOCKET sock) // send loop for multithreading
		{
			{ // prevents prompt from causing overlapping console outputs
				std::lock_guard<std::mutex> consoleLock(consoleMutex); // mutex lock for overlapping console outputs

				std::cout << "\r=== " << Interactions::serverHandle << "@" << IPv4str(Endpoint::ipLocal) << "'S CHATROOM ===" << std::endl;
			}
			packetOut(sock , "\r=== WELCOME TO " + Interactions::serverHandle + "@" + IPv4str(Endpoint::ipLocal) + "'S CHATROOM ===");

			while (true)
			{
				packetOut(sock);
			}


			return;
		}
};


int main()
{
	sock sockServer; // socket()

	Interactions::inputPort();

	sockServer.sockBind(Endpoint::sockaddrServer);

	sockServer.sockListen();

	sock sockClient(sockServer.sockAccept(Endpoint::sockaddrClient)); // waiting for connect()<-

	Interactions::inputHandle();

	Protocol protocol;


	protocol.packetOut(sockClient.sockfd , Interactions::serverHandle);


	std::thread recvThread(&Protocol::packetInLoop , &protocol , sockClient.sockfd);
	std::thread sendThread(&Protocol::packetOutLoop , &protocol , sockClient.sockfd);

	recvThread.join();
	sendThread.join();


	return 0;
}

//- todo
//- multiple clients