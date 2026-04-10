import os
import sys
import struct
import fcntl
import ctypes
import socket
import errno

DEVICE = "/dev/int_stack"
SOCKET_PATH = "/tmp/kernel_stack.sock"

def IOW(magic, nr, size):
    return (1 << 30) | (size << 16) | (ord(magic) << 8) | nr

def IOR(magic, nr, size):
    return (2 << 30) | (size << 16) | (ord(magic) << 8) | nr

INT_SIZE = ctypes.sizeof(ctypes.c_int)
SET_SIZE = IOW('k', 1, INT_SIZE)
GET_SIZE = IOR('k', 2, INT_SIZE)

def handle_command(fd, command):
    parts = command.strip().split()
    if not parts:
        return "ERROR: empty command"

    cmd = parts[0]

    if cmd == "set-size":
        size = int(parts[1])
        if size <= 0:
            return "ERROR: size should be > 0"
        try:
            fcntl.ioctl(fd, SET_SIZE, struct.pack("i", size))
            return "ok"
        except OSError as e:
            if e.errno == errno.ERANGE:
                return "ERROR: stack is full"
            return f"ERROR: {e}"

    elif cmd == "push":
        value = int(parts[1])
        try:
            os.write(fd, struct.pack("i", value))
            return "ok"
        except OSError as e:
            if e.errno == errno.ERANGE:
                return "ERROR: stack is full"
            return f"ERROR: {e}"

    elif cmd == "pop":
        try:
            data = os.read(fd, 4)
            if not data:
                return "NULL"
            return str(struct.unpack("i", data)[0])
        except OSError as e:
            return f"ERROR: {e}"

    elif cmd == "unwind":
        results = []
        while True:
            try:
                data = os.read(fd, 4)
                if not data:
                    break
                results.append(str(struct.unpack("i", data)[0]))
            except OSError:
                break
        return "\n".join(results) if results else "NULL"

    elif cmd == "get-size":
        try:
            buf = fcntl.ioctl(fd, GET_SIZE, struct.pack("i", 0))
            return str(struct.unpack("i", buf)[0])
        except OSError as e:
            return f"ERROR: {e}"

    elif cmd == "stop":
        return "stopping"

    return "ERROR: unknown command"

def cleanup(server):
    server.close()
    if os.path.exists(SOCKET_PATH):
        os.unlink(SOCKET_PATH)

def main():
    try:
        fd = os.open(DEVICE, os.O_RDWR)
    except OSError as e:
        print(f"ERROR: cannot open {DEVICE}: {e}")
        sys.exit(1)

    if os.path.exists(SOCKET_PATH):
        os.unlink(SOCKET_PATH)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCKET_PATH)
    server.listen(5)

    print(f"daemon started, fd={fd}")

    try:
        while True:
            conn, _ = server.accept()
            try:
                data = conn.recv(256).decode()
                if not data:
                    conn.close()
                    continue
                response = handle_command(fd, data)
                conn.send(response.encode())
            except Exception as e:
                conn.send(f"ERROR: {e}".encode())
            finally:
                conn.close()
            if response == "stopping":
                break
    finally:
        cleanup(server)

if __name__ == "__main__":
    main()
