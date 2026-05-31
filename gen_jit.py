import re

def emit(bytes_str):
    bytes_list = bytes_str.replace('\n', ' ').split()
    out = ""
    for b in bytes_list:
        if len(b) == 2:
            out += f"jit_emit_byte(&buf, 0x{b}); "
    print(out)

print("OP_INDEX:")
emit("49 bb ff ff ff ff ff ff 00 00 4c 21 d8 4d 21 da 4d 8b 5a 28 49 8b 04 c3")

print("\nOP_STORE_INDEX:")
emit("49 ff cd 4f 8b 0c ec 49 bb ff ff ff ff ff ff 00 00 4c 21 d8 4d 21 da 4d 8b 5a 28 4d 89 0c c3")
