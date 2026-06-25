import sys
import struct

SOF         = b'\xaa\xbb'  # SOF - Little Endian 0xBBAA
JPEG_SOI    = b'\xff\xd8'  # Start of Image
JPEG_EOI    = b'\xff\xd9'  # End of Image

def visualize_stream(file_path):
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)

    file_len = len(data)
    offset = 0

    while offset < file_len - 17:
        cursor = offset
        hdr_size = 2

        hdr_bytes = data[cursor : cursor + hdr_size]
        cursor += 2

        dev_id = data[cursor]
        cursor += 1

        frm_len = struct.unpack_from('<H', data[cursor: cursor + 2])[0]
        cursor += 2
        expected_eof = cursor + frm_len

        frm_id = data[cursor]
        cursor += 1

        dev_num = data[cursor]
        cursor += 1

        flags = data[cursor]
        cursor += 1

        gs = data[cursor : cursor + 4]
        cursor += 4

        if hdr_bytes == SOF:
            # Found 0xBBAA (Stored as AA BB over wire)
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [SOF] | dev_id: {dev_id} frm: {frm_id} dev_num: {dev_num} flags: {flags} len: {frm_len} data_end: {expected_eof}")
            offset += hdr_size # Move past the full marker
            continue

        elif hdr_bytes == JPEG_SOI:
            # Found standard JPEG Start of Image
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [JPEG SOI]")
            offset += hdr_size
            continue

        elif hdr_bytes == JPEG_EOI:
            # Found standard JPEG End of Image
            print(f"{offset:<15} | {f'0x{offset:08X}':<15} | [JPEG EOI]")
            offset += hdr_size
            continue

        # No match found, shift scanning window by exactly 1 byte
        offset += 1

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python visualize_markers.py <binary_stream.bin>")
        sys.exit(1)

    visualize_stream(sys.argv[1])
