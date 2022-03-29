#!/usr/bin/python3

"""
argument format:
    ./script.py target action
    example:
    ./script.py light on
    ./script.py light dim 50
    ./script.py audio off
"""

import serial
import sys
import time

PACKET_MAXLEN = 8
BAUDRATE = 9600
DEFAULT_PORT = "/dev/ttyUSB0"
port = DEFAULT_PORT

class protocol:
    commands = {
            "ccfl_on"       : 0x81,
            "ccfl_off"      : 0x82,
            "ccfl_set"      : 0x83,
            "ccfl_add"      : 0x84,
            "ccfl_sub"      : 0x85,
            "ccfl_toggle"   : 0x86,
            "audio_on"      : 0x71,
            "audio_off"     : 0x72,
            "audio_toggle"  : 0x73,
    }

def errPrint(msg):
    print(f"ERROR: {msg}")

def printHelp():
    print("Usage:")
    print(f"{sys.argv[0]} target command(s)")
    print("Available targets:")
    print("\t audio")
    print("\t ccfl")
    print("Example:")
    print(f"{sys.argv[0]} audio toggle")

class devices:
    def ccfl(args):

        if args[0] == "on":
            send(protocol.commands['ccfl_on'])

        elif args[0] == "off":
            send(protocol.commands['ccfl_off'])

        elif args[0] == "dim":
            dimVal = args[1]
            if dimVal[0] == "+":
                send(protocol.commands['ccfl_add'], [int(dimVal[1:])])
            elif dimVal[0] == "-":
                send(protocol.commands['ccfl_sub'], [int(dimVal[1:])])
            else:
                send(protocol.commands['ccfl_set'], [int(dimVal)])

        elif args[0] == "toggle":
            send(protocol.commands['ccfl_toggle'])
        else:
            errPrint(f"Wrong command for ccfl target: {args[0]}")


    def audioRelay(args):
        if args[0] == "on":
            send(protocol.commands['audio_on'])

        elif args[0] == "off":
            send(protocol.commands['audio_off'])

        elif args[0] == "toggle":
            send(protocol.commands['audio_toggle'])

        else:
            errPrint(f"Wrong command for audio target: {args[0]}")

targets = {"light" : devices.ccfl, "audio" : devices.audioRelay }

def init(port):
    global ser
    ser = serial.Serial(port, BAUDRATE)

def send(com, args=None):
    com = b"SI" + com.to_bytes(1, "big")
    if args:
        for i in args:
            com += i.to_bytes(1, "big")
    if len(com) > PACKET_MAXLEN:
        raise ValueError
    #pad the message for serial transmission optimization
    payload = com + b"\x00" * (PACKET_MAXLEN - len(com))
    ser.write(payload)

try:
    target = sys.argv[1]
    if target not in targets:
        errPrint(f"Invalid target: {target}!")
        printHelp()
        sys.exit(1)
    init(port)
    args = sys.argv[2:]
    targets[target](args)
except IndexError:
    errPrint("Not enough arguments!")
    printHelp()
    sys.exit(1)
