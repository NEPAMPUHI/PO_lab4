#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <vector>
#include <string>
#include <thread>

using namespace std;

struct Client {
	vector<vector<uint16_t>> matrix;
	uint16_t threadNum = 0;
	bool status = false;
};

/*void place_row_max_on_main_diagonal(vector<vector<uint16_t>>& matrix) { //non-parallel
	int max;
	int max_i = 0;
	int max_j = 0;
	int matrix_size = matrix.size();
	for (int i = 0; i < matrix_size; i++) {
		max = matrix[i][0];
		for (int j = 1; j < matrix_size; j++) {
			if (matrix[i][j] > max) {
				max = matrix[i][j];
				max_i = i;
				max_j = j;
			}
		}
		matrix[max_i][max_j] = matrix[i][i];
		matrix[i][i] = max;
	}
}*/

void process_matrix_section(vector<vector<uint16_t>>& matrix, size_t first, size_t last) {
	int max;
	int max_i;
	int max_j;
	int matrix_size = matrix.size();
	for (int i = first; i <= last; i++) {
		max = matrix[i][0];
		max_i = i;
		max_j = 0;
		for (int j = 1; j < matrix_size; j++) {
			if (matrix[i][j] > max) {
				max = matrix[i][j];
				max_i = i;
				max_j = j;
			}
		}
		matrix[max_i][max_j] = matrix[i][i];
		matrix[i][i] = max;
	}
}

void modify_matrix(Client& client) {
	client.status = true;
	vector<thread> threads;
	int rowsMin = client.matrix.size() / client.threadNum;
	int rowsExtra = client.matrix.size() % client.threadNum;

	for (int i = 0; i < client.threadNum; i++) {
		int first = i * rowsMin + (i < rowsExtra ? i : rowsExtra);
		int last = first + rowsMin + (i < rowsExtra ? 0 : -1);
		threads.emplace_back(process_matrix_section, client.matrix, first, last);
	}

	for (auto& th : threads) {
		th.join();
	}

	client.status = false;
}

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

bool send_message(SOCKET sock, const string& message) {
	uint16_t len = htons(static_cast<uint16_t>(message.size()));
	if (!send_all(sock, (char*)len, sizeof(len))) {
		cerr << "Failed to send length\n";
		return false;
	}
	if (!send_all(sock, message.c_str(), message.size())) {
		cerr << "Failed to send message\n";
		return false;
	}
	return true;
}

bool receive_data(SOCKET sock, vector<vector<uint16_t>>& matrix, size_t thread_num) {
	uint8_t tag;
	uint16_t len;

	if (recv(sock, (char*)tag, sizeof(tag), 0) == SOCKET_ERROR) {
		cerr << "Receiving tag failed\n";
		return false;
	}

	if (recv(sock, (char*)len, sizeof(len), 0) == SOCKET_ERROR) {
		cerr << "Receiving amount failed\n";
		return false;
	}
	len = ntohs(len);

	if (tag == 0x01) {
		for (int i = 0; i < len; i++) {
			for (int j = 0; j < len; j++) {
				if (recv(sock, (char*)matrix[j][i], sizeof(uint16_t), 0) == SOCKET_ERROR) {
					cerr << "Receiving length failed\n";
					return 0;
				}
				matrix[j][i] = ntohs(matrix[j][i]);
			}

		}
	}
	else if (tag == 0x02) thread_num = len;
	else {
		cerr << "Invalid tag\n";
		return false;
	}
	return true;
}

void handle_client(SOCKET sock) {
	Client client;
	bool shouldContinue = true;
	//TODO: correct communication
	try {
		while (shouldContinue) {
			if (!send_message(sock, "Waiting for the matrix dimension and matrix values...\n")) 
				throw runtime_error("Something went wrong :(");
			if (!receive_data(sock, client.matrix, client.threadNum)) {
				if (!send_message(sock, "Something is wrong with your data. \nDisconnecting...\n")) return;
			}
			if (!send_message(sock, "Waiting for the number of threads...\n")) return;
			if (!receive_data(sock, client.matrix, client.threadNum)) {
				if (!send_message(sock, "Something is wrong with your data. \nDisconnecting...\n")) return;
			}
			if (!send_message(sock, "Send START to modify matrix\n")) return;
			//answer

			thread(modify_matrix, client);
			if (!send_message(sock, "Send GET to receive the result\n")) return;
		}
	}
	catch (const exception& e) {
		cerr << "Something went wrong with client " << sock << ": " << e.what() << endl;
		send_message(sock, e.what());
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

	return 0;
}