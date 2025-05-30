#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

bool send_all(SOCKET sock, const char* buf, int len) {
	int total = 0;
	while (total < len) {
		int sent = send(sock, buf + total, len - total, 0);
		if (sent == SOCKET_ERROR) return false;
		total += sent;
	}
	return true;
}

bool recv_all(SOCKET sock, char* buf, int len) {
	int total = 0;
	while (total < len) {
		int rec = recv(sock, buf + total, len - total, 0);
		if (rec <= 0) return false;
		total += rec;
	}
	return true;
}

struct Client {
	vector<vector<uint16_t>> matrix;
	uint16_t dimension;
	uint16_t threadNum;
	SOCKET sock;
	bool is_active;
	mutex mtx;
	condition_variable cond_var;

	Client(SOCKET sock) : dimension(0), threadNum(0), sock(sock), is_active(false) {}

	bool send_us_matrix() {
		uint8_t tag;
		uint16_t len;

		if (recv(sock, reinterpret_cast<char*>(&tag), sizeof(tag), 0) == SOCKET_ERROR) {
			cerr << "Receiving tag failed\n";
			return false;
		}

		if (recv(sock, reinterpret_cast<char*>(&len), sizeof(len), 0) == SOCKET_ERROR) {
			cerr << "Receiving matrix dimension failed\n";
			return false;
		}
		len = ntohs(len);
		dimension = len;

		if (tag == 0x01) {
			matrix.assign(len, vector<uint16_t>(len));
			for (int i = 0; i < len; i++) {
				for (int j = 0; j < len; j++) {
					if (recv(sock, (char*)matrix[i][j], sizeof(uint16_t), 0) == SOCKET_ERROR) {
						cerr << "Receiving values failed\n";
						return 0;
					}
					matrix[i][j] = ntohs(matrix[i][j]);
				}
			}
		}
		else {
			cerr << "Invalid tag\n";
			return false;
		}
		return true;
	}

	bool send_us_thread_num() {
		uint8_t tag;
		uint16_t len;

		if (recv(sock, reinterpret_cast<char*>(&tag), sizeof(tag), 0) == SOCKET_ERROR) {
			cerr << "Receiving tag failed\n";
			return false;
		}

		if (recv(sock, reinterpret_cast<char*>(&len), sizeof(len), 0) == SOCKET_ERROR) {
			cerr << "Receiving thread number failed\n";
			return false;
		}
		len = ntohs(len);

		if (tag == 0x02) threadNum = len;
		else {
			cerr << "Invalid tag\n";
			return false;
		}
		return true;
	}

	bool send_us_TLV(string& value) {
		uint8_t tag;
		uint8_t len;

		if (recv(sock, reinterpret_cast<char*>(&tag), sizeof(tag), 0) == SOCKET_ERROR) {
			cerr << "Receiving tag failed\n";
			return false;
		}

		if (tag != 0x03) {
			cerr << "Incorrect tag\n";
			return false;
		}

		if (recv(sock, reinterpret_cast<char*>(&len), sizeof(len), 0) == SOCKET_ERROR) {
			cerr << "Receiving length failed\n";
			return false;
		}

		vector<char> buf(len);
		if (!recv_all(sock, buf.data(), len)) {
			cerr << "Receiving value failed\n";
			return false;
		}
		value = string(buf.begin(), buf.end());
		return true;
	}

	bool receive_message(const string& message) {
		uint16_t len = htons(static_cast<uint16_t>(message.size()));
		if (!send_all(sock, reinterpret_cast<char*>(&len), sizeof(len))) {
			cerr << "Failed to send length\n";
			return false;
		}
		if (!send_all(sock, message.c_str(), message.size())) {
			cerr << "Failed to send message\n";
			return false;
		}
		return true;
	}

	void process_matrix_section(size_t first, size_t last) {
		int max;
		size_t max_i;
		size_t max_j;
		for (size_t j = first; j <= last; j++) {
			max = matrix[0][j];
			max_i = 0;
			max_j = j;
			for (size_t i = 1; i < dimension; i++) {
				if (matrix[i][j] > max) {
					max = matrix[i][j];
					max_i = i;
					max_j = j;
				}
			}
			matrix[max_i][max_j] = matrix[j][j];
			matrix[j][j] = max;
		}
	}

	void modify_matrix() {
		{
			lock_guard<mutex> lock(mtx);
			is_active = true;
		}
		vector<thread> threads;
		int col_min = dimension / threadNum;
		int col_extra = dimension % threadNum;

		for (int i = 0; i < threadNum; i++) {
			int first = i * col_min + (i < col_extra ? i : col_extra);
			int last = first + col_min + (i < col_extra ? 0 : -1);
			threads.emplace_back(&Client::process_matrix_section, this, first, last);
		}

		for (auto& th : threads) {
			th.join();
		}

		{
			lock_guard<mutex> lock(mtx);
			is_active = false;
		}

		cond_var.notify_one();
	}
};

void handle_client(SOCKET sock) {
	Client client(sock);
	bool should_continue = true;
	string error_message = "Something is wrong. Try again\n";
	string client_answer = "";

	try {
		while (should_continue) {
			if (!client.receive_message("Waiting for the matrix dimension and matrix values...\n"))
				throw runtime_error("");
			if (!client.send_us_matrix()) {
				if (!client.receive_message(error_message))
					throw runtime_error("");
				continue;
			}

			if (!client.receive_message("Waiting for the thread number...\n"))
				throw runtime_error("");
			if (!client.send_us_thread_num()) {
				if (!client.receive_message(error_message))
					throw runtime_error("");
				continue;
			}

			if (client.matrix.empty() || client.threadNum == 0) {
				if (!client.receive_message(error_message))
					throw runtime_error("");
				continue;
			}
			if (!client.receive_message("Your data has been accepted. To start modifying the matrix, send START\n"))
				throw runtime_error("");
			if (!client.send_us_TLV(client_answer)) {
				if (!client.receive_message(error_message))
					throw runtime_error("");
				continue;
			}

			while (client_answer != "START") {
				if (!client.receive_message("Don't be an idiot. Just send START.\n"))
					throw runtime_error("");

				if (!client.send_us_TLV(client_answer)) {
					if (!client.receive_message(error_message))
						throw runtime_error("");
				}
			}

			if (!client.receive_message("Modification has started. To see the status of the process, send STATUS. To get the result, send RESULT.\n"))
				throw runtime_error("");
			thread(&Client::modify_matrix, &client).detach();

			while (should_continue) {
				if (!client.send_us_TLV(client_answer)) {
					if (!client.receive_message(error_message))
						throw runtime_error("");
					continue;
				}

				if (client_answer == "STATUS") {
					client_answer = "Status: " + client.is_active ? "in progress" : "completed";
					client_answer += "\nTo see the status of the process, send STATUS. To get the result, send RESULT.\n ";
					if (!client.receive_message(client_answer))
						throw runtime_error("");
				}
				else if (client_answer == "RESULT") {
					unique_lock<mutex> lock(client.mtx);
					client.cond_var.wait(lock, [&client] { return client.is_active == false;  });

					if (!send_all(sock, reinterpret_cast<char*>(&client.dimension), sizeof(client.dimension))) {
						throw runtime_error("Failed to send matrix dimension to client");
					}
					for (int i = 0; i < client.dimension; i++) {
						for (int j = 0; j < client.dimension; j++) {
							uint16_t number = htons(client.matrix[i][j]);
							if (!send_all(sock, reinterpret_cast<char*>(&number), sizeof(number))) {
								throw runtime_error("Failed to send matrix to client");
							}
						}
					}
					should_continue = false;
				}
				else {
					if (!client.receive_message("Invalid input. Only STATUS and RESULT are allowed\n"))
						throw runtime_error("");
				}
			}

			if (!client.receive_message("You have received your processed matrix.That's all. Bye-bye.\n"))
				throw runtime_error("");
			should_continue = false;
			closesocket(sock);
		}
	}
	catch (const exception& e) {
		cerr << "Something went wrong with client " << sock << endl << e.what() << endl;
	}
	closesocket(sock);
}

int main() {
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		cerr << "WSAStartup failed\n";
		return 1;
	}

	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == INVALID_SOCKET) {
		cerr << "Socket creation failed\n";
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serverAddr.sin_port = htons(9000);

	if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		cerr << "Bind failed\n";
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	if (listen(serverSocket, 5) == SOCKET_ERROR) {
		cerr << "Bind failed\n";
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}

	cout << "Server is waiting...\n";

	while (true) {
		sockaddr_in clientAddr{};
		int clientSize = sizeof(clientAddr);
		SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientSize);
		if (clientSocket == INVALID_SOCKET) {
			cerr << "Client socket creation failed\n";
			continue;
		}

		cout << "New client is connected. Socket: " << clientSocket << endl;

		thread(handle_client, clientSocket).detach();
	}

	WSACleanup();
	return 0;
}