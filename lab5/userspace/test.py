import os
import time
import struct

fd = os.open("/dev/int_stack_wk", os.O_RDWR)

try:
    while True:
        val = 42
        os.write(fd, struct.pack("i", val))
        data = os.read(fd, 4)
        if not data:
            print("EOF or empty read")
            break

        val = struct.unpack("i", data)[0]
        print("read:", val)

        time.sleep(1)

except OSError as e:
    print("Error:", e)

finally:
    os.close(fd)