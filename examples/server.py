import zmq

FILE_NAME = "messages.txt"

context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://0.0.0.0:5566")

print("ZMQ server started")

counter = 0

while True:
    msg = socket.recv_string()
    counter += 1

    print(f"[{counter}] Received: {msg}")

    with open(FILE_NAME, "a", encoding="utf-8") as f:
        f.write(f"[{counter}] {msg}\n")

    socket.send_string("Hello from Server!")