import argparse
import errno
import os
import socket
import subprocess
import sys
import time

SOCKET_PATH = "/tmp/kernel_stack.sock"
DAEMON_SCRIPT = os.path.join(os.path.dirname(__file__), "daemon.py")

def ensure_daemon():
    if os.path.exists(SOCKET_PATH):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCKET_PATH)
            s.close()
            return
        except OSError:
            os.unlink(SOCKET_PATH)

    subprocess.Popen(
        [sys.executable, DAEMON_SCRIPT],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    for _ in range(20):
        time.sleep(0.1)
        if os.path.exists(SOCKET_PATH):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(SOCKET_PATH)
                s.close()
                return
            except OSError:
                continue

    print("ERROR: daemon failed to start", file=sys.stderr)
    sys.exit(1)

def send_command(command):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCKET_PATH)
    s.send(command.encode())
    response = s.recv(256).decode()
    s.close()
    return response

def handle_response(response, silent_ok=False):
    if response.startswith("ERROR:"):
        print(response)
        if "stack is full" in response:
            sys.exit(-errno.ERANGE)
        sys.exit(1)
    if not silent_ok:
        print(response)

def main():
    parser = argparse.ArgumentParser(prog="kernel_stack")
    subparsers = parser.add_subparsers(dest="command")

    p_set = subparsers.add_parser("set-size")
    p_set.add_argument("size", type=int)

    p_push = subparsers.add_parser("push")
    p_push.add_argument("value", type=int)

    subparsers.add_parser("pop")
    subparsers.add_parser("unwind")
    subparsers.add_parser("get-size")
    subparsers.add_parser("stop")

    args = parser.parse_args()

    if args.command is None:
        parser.print_help()
        sys.exit(1)

    if args.command == "stop":
        if not os.path.exists(SOCKET_PATH):
            print("ERROR: daemon is not running")
            sys.exit(1)
        send_command("stop")
        return

    ensure_daemon()

    if args.command == "set-size":
        handle_response(send_command(f"set-size {args.size}"), silent_ok=True)
    elif args.command == "push":
        handle_response(send_command(f"push {args.value}"), silent_ok=True)
    elif args.command == "pop":
        handle_response(send_command("pop"))
    elif args.command == "unwind":
        handle_response(send_command("unwind"))
    elif args.command == "get-size":
        handle_response(send_command("get-size"))

if __name__ == "__main__":
    main()
