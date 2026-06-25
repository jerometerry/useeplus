import usb.core
import usb.util
import sys
import time

VID = 0x0329
PID = 0x2022
TARGET_INDEX = 1        # 1: 640x480, 2: 320x240, 3: 1280x720
OUTPUT_FILE = "raw_camera_stream.bin"
CAPTURE_DURATION_SEC = 5 # How long to record the feed

dev = usb.core.find(idVendor=VID, idProduct=PID)
if dev is None:
    print(f"Error: Camera (VID: {hex(VID)}, PID: {hex(PID)}) not found!")
    sys.exit(1)

for intf in dev.get_active_configuration():
    if dev.is_kernel_driver_active(intf.bInterfaceNumber):
        try:
            dev.detach_kernel_driver(intf.bInterfaceNumber)
            print(f"Detached kernel driver on interface {intf.bInterfaceNumber}")
        except Exception as e:
            print(f"Could not detach interface {intf.bInterfaceNumber}: {e}")

dev.set_configuration()

print("--> Activating Video Interface Alternate Setting 1...")
dev.set_interface_altsetting(interface=1, alternate_setting=1)

usb.util.claim_interface(dev, 0)
usb.util.claim_interface(dev, 1)

payload = [0] * 26
payload[2] = 0x02  # MJPEG Format Index
payload[3] = TARGET_INDEX
interval = 10000000 // 30  # Hardcode 30 FPS tick units
payload[4:8] = [interval & 0xFF, (interval >> 8) & 0xFF, (interval >> 16) & 0xFF, (interval >> 24) & 0xFF]

print("--> Initializing Setup Control Sequence...")
dev.ctrl_transfer(0x21, 0x01, 0x0100, 1, payload)  # PROBE
dev.ctrl_transfer(0x21, 0x01, 0x0200, 1, payload)  # COMMIT

print("--> Firing iAP Auth Handshake Sequence...")
dev.write(0x02, [0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10])  # EP 2 OUT

print("--> Sending Start Video Stream Token...")
dev.write(0x01, [0xBB, 0xAA, 0x05, 0x00, 0x00])        # EP 1 OUT

print(f"\n[+] Initialization successful. Capturing stream into '{OUTPUT_FILE}' for {CAPTURE_DURATION_SEC}s...")
bytes_captured = 0
start_time = time.time()

with open(OUTPUT_FILE, "wb") as bin_out:
    try:
        while time.time() - start_time < CAPTURE_DURATION_SEC:
            data_packet = dev.read(0x81, 65536, timeout=1000)

            if data_packet:
                bin_out.write(data_packet)
                bytes_captured += len(data_packet)

    except usb.core.USBError as e:
        if e.errno == 110 or "timeout" in str(e).lower():
            pass
        else:
            print(f"\n[!] USB Data pipe stream disruption: {e}")
    except KeyboardInterrupt:
        print("\n[-] Stream capture interrupted manually by user.")

print(f"\n[+] Stream Stopped. Successfully stored {bytes_captured} raw bytes.")
print("--> Releasing hardware interface hooks...")
usb.util.release_interface(dev, 0)
usb.util.release_interface(dev, 1)
print("[+] System complete. Ready to feed binary layout to your protocol visualizer.")
