import socket

HOST = "127.0.0.1"
PORT = 5050

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))

s.sendall(b"Hello")

data = s.recv(1024)
print("Сервер ответил:", data.decode())

s.close()

#Сокет (socket) — это конечная точка соединения между двумя программами, которые хотят обмениваться данными.
