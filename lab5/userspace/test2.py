import os
import struct
import errno

def push(fd, val):
    os.write(fd, struct.pack("i", val))
    print(f"pushed: {val}")

def pop(fd):
    data = os.read(fd, 4)
    if not data:
        print("stack empty")
        return
    print(f"popped: {struct.unpack('i', data)[0]}")

fd = os.open("/dev/int_stack_wk", os.O_RDWR)

push(fd, 1)
push(fd, 2)
push(fd, 3)

input("Извлеките USB и нажмите Enter...")

try:
    push(fd, 99)
except OSError as e:
    if e.errno == errno.ENODEV:
        print(f"Error: USB key is not inserted ({e})")
    else:
        raise

input("Вставьте USB обратно и нажмите Enter...")

pop(fd)
pop(fd)
pop(fd)

os.close(fd)