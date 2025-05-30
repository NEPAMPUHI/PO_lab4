import socket
import struct
import threading
import random
import time
import sys

THREADS_NUM = 4
MATRIX_SIZE = 1000
SERVER_IP = '192.168.89.178'
SERVER_PORT = 9000

matrix = [[0] * MATRIX_SIZE for _ in range(MATRIX_SIZE)]

def fill_part_of_matrix(first: int, last: int):
    for i in range(first, last + 1):
        row = matrix[i]
        for j in range(MATRIX_SIZE):
            row[j] = random.randint(0, 0xFFFF)

def multithreaded_filling_matrix():
    threads = []
    rows_min = MATRIX_SIZE // THREADS_NUM
    rows_extra = MATRIX_SIZE % THREADS_NUM

    for i in range(THREADS_NUM):
        first = i * rows_min + (i if i < rows_extra else rows_extra)
        last  = first + rows_min - 1 + (1 if i < rows_extra else 0)
        th = threading.Thread(target = fill_part_of_matrix, args = (first, last))
        th.start()
        threads.append(th)

    for th in threads:
        th.join()

def is_matrix_correct() -> bool:
    for j in range(MATRIX_SIZE):
        pivot = matrix[j][j]
        for i in range(MATRIX_SIZE):
            if matrix[i][j] > pivot:
                return False
    return True

def recv_all(sock: socket.socket, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            raise RuntimeError('Unexpected EOF during recv_all')
        data.extend(packet)
    return bytes(data)

def recv_LV(sock: socket.socket) -> str:
    raw_len = recv_all(sock, 2)
    (length,) = struct.unpack('!H', raw_len)
    payload = recv_all(sock, length)
    return payload.decode('utf-8', errors='replace')

def send_TLV(sock: socket.socket, tag: int, value: str):
    bval = value.encode('utf-8')
    if len(bval) > 255:
        raise ValueError('Value too long for TLV')
    pkt = struct.pack('B', tag) \
        + struct.pack('B', len(bval)) \
        + bval
    sock.sendall(pkt)

def send_matrix(sock: socket.socket):
    sock.sendall(b'\x01')
    sock.sendall(struct.pack('!H', MATRIX_SIZE))
    for i in range(MATRIX_SIZE):
        for j in range(MATRIX_SIZE):
            sock.sendall(struct.pack('!H', matrix[i][j]))

def send_thread_num(sock: socket.socket):
    sock.sendall(b'\x02')
    sock.sendall(struct.pack('!H', THREADS_NUM))

def main():
    multithreaded_filling_matrix()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((SERVER_IP, SERVER_PORT))
    except Exception as e:
        print('Connection error:', e)
        sys.exit(1)

    try:
        print(recv_LV(sock), end='')

        send_matrix(sock)
        print('Matrix sent.\n')

        print(recv_LV(sock), end='')

        send_thread_num(sock)
        print('Thread number sent.\n')

        print(recv_LV(sock), end='')

        send_TLV(sock, 0x03, 'START')
        print('START sent.\n')

        print(recv_LV(sock), end='')

        while True:
            send_TLV(sock, 0x03, 'STATUS')
            print('STATUS sent.')
            reply = recv_LV(sock)
            print(reply, end='')
            if 'completed' in reply:
                break
            time.sleep(0.02)

        send_TLV(sock, 0x03, 'RESULT')
        print('RESULT sent.\n')

        raw_n = recv_all(sock, 2)
        (n,) = struct.unpack('!H', raw_n)
        if n != MATRIX_SIZE:
            raise RuntimeError(f'Unexpected dimension {n}')
        for i in range(n):
            for j in range(n):
                raw_v = recv_all(sock, 2)
                (v,) = struct.unpack('!H', raw_v)
                matrix[i][j] = v

        print('Matrix is', 'correct' if is_matrix_correct() else 'NOT correct')

        print(recv_LV(sock), end='')

    finally:
        sock.close()

if __name__ == '__main__':
    main()

