import socket

HOST = "0.0.0.0"
PORT = 5050

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind((HOST, PORT))
s.listen(1)

print("Ожидание от сервера")

conn, addr = s.accept()
print(f"Подключено {addr}")

data = conn.recv(1024).decode()
print("Клиент отправил::", data)

conn.sendall(b"Hello")

conn.close()