#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <vector>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

static const uint16_t threads_num = 4;
static const uint16_t matrix_size = 1000;
uint16_t matrix[matrix_size][matrix_size];

void fill_part_of_matrix(size_t first, size_t last) {
    for (int i = first; i <= last; i++) {
        for (int j = 0; j < matrix_size; j++) {
            matrix[i][j] = rand();
        }
    }
}

void multithreaded_filling_matrix() {
    thread threads[threads_num];

    int rows_min = matrix_size / threads_num;
    int rows_extra = matrix_size % threads_num;

    for (int i = 0; i < threads_num; i++) {
        int first = i * rows_min + (i < rows_extra ? i : rows_extra);
        int last = first + rows_min + (i < rows_extra ? 0 : -1);
        threads[i] = thread(fill_part_of_matrix, first, last);
    }

    for (auto& th : threads) {
        th.join();
    }
}

bool is_matrix_correct() {
    for (int j = 0; j < matrix_size; j++) {
        for (int i = 0; i < matrix_size; i++) {
            if (matrix[i][j] > matrix[j][j]) {
                return false;
            }
        }
    }
    return true;
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

string recv_LV(SOCKET sock) {
	uint16_t len;
	if (!recv_all(sock, reinterpret_cast<char*>(&len), sizeof(len))) {
		throw runtime_error("Failed to receive length");
	}
	len = ntohs(len);
	string message(len, '\0');
	if (!recv_all(sock, &message[0], len)) {
		throw runtime_error("Failed to receive message");
	}
	return message;
}

void send_TLV(SOCKET sock, uint8_t tag, const string& value) { //Tag: 0x03 (1 byte) + Length (1 byte) + Value: "START "
    if (value.size() > 255) 
        throw runtime_error("Value is too long");
    uint8_t len = static_cast<uint8_t>(value.size());
    vector<char> buf;
    buf.reserve(2 + len);
    buf.push_back(static_cast<char>(tag));
    buf.push_back(static_cast<char>(len));
    buf.insert(buf.end(), value.begin(), value.end());
    if (!send_all(sock, buf.data(), (int)buf.size())) {
        throw runtime_error("Failed to send TLV");
    }
}

void send_matrix(SOCKET sock, uint8_t tag) {//Tag (0x01, 1 byte) + Matrix dimension N (2 bytes) + Matrix data (N*N numbers, 2 bytes per number)
    if (send(sock, reinterpret_cast<char*>(&tag), sizeof(tag), 0) == SOCKET_ERROR)
        throw runtime_error("Failed to send tag");

    uint16_t len = htons(matrix_size);
    if (!send_all(sock, reinterpret_cast<char*>(&len), sizeof(len))) 
        throw runtime_error("Failed to send matrix dimension");
   
    for (size_t i = 0; i < matrix_size; i++) {
        for (size_t j = 0; j < matrix_size; j++) {
            uint16_t value = htons(matrix[i][j]);
            if (!send_all(sock, reinterpret_cast<char*>(&value), sizeof(value)))
                throw runtime_error("Failed to send matrix");
        }
    }
}

void send_thread_num(SOCKET sock, uint8_t tag) { //Tag(0x02, 1 byte) + Number of threads(2 bytes)
    if (send(sock, reinterpret_cast<char*>(&tag), sizeof(tag), 0) == SOCKET_ERROR)
        throw runtime_error("Failed to send tag");

    uint16_t t_num = htons(threads_num);
    if (!send_all(sock, reinterpret_cast<char*>(&t_num), sizeof(t_num)))
        throw runtime_error("Failed to send number of threads");
}

int main() {
    srand(time(nullptr));
    multithreaded_filling_matrix();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "socket() failed\n";
        WSACleanup();
        return 1;
    }
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9000);
    serv_addr.sin_addr.s_addr = inet_addr("192.168.89.178");

    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cerr << "connect() failed\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    try {
        cout << recv_LV(sock); //"Waiting for the matrix dimension and matrix values...\n"

        send_matrix(sock, 0x01);
        cout << "Matrix is sended" << endl << endl;

        cout << recv_LV(sock); //"Waiting for the thread number...\n"
        send_thread_num(sock, 0x02);
        cout << "Thread number is sended" << endl << endl;

        cout << recv_LV(sock); //"Your data has been accepted. To start modifying the matrix, send START\n"
        send_TLV(sock, 0x03, "START");
        cout << "START" << endl << endl;

        cout << recv_LV(sock); //"Modification has started. To see the status of the process, send STATUS. To get the result, send RESULT.\n"

        while (true) {
            send_TLV(sock, 0x03, "STATUS");
            cout << "STATUS" << endl << endl;
            string reply = recv_LV(sock); //"Status: … (in progress / completed)\nTo see the status of the process, send STATUS. To get the result, send RESULT.\n"
            cout << reply;
            if (reply.find("completed") != string::npos)
                break;  
            this_thread::sleep_for(chrono::milliseconds(20));
        }

        send_TLV(sock, 0x03, "RESULT");
        cout << "RESULT" << endl << endl;

        uint16_t n_net;
        recv_all(sock, reinterpret_cast<char*>(&n_net), sizeof(n_net));
        uint16_t n = ntohs(n_net);
        if (n == matrix_size) {
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    uint16_t value;
                    recv_all(sock, reinterpret_cast<char*>(&value), sizeof(value));
                    matrix[i][j] = ntohs(value);
                }
            }
        } else 
            throw runtime_error("Incorrect matrix dimension");

        cout << "Matrix is " << (is_matrix_correct() ? "" : "NOT ") << "correct" << endl;

        cout << recv_LV(sock); //"You have received your processed matrix. That's all. Bye-bye.\n"

    }
    catch (const exception& ex) {
        cerr << "Error: " << ex.what() << "\n";
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}