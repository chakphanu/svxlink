#!/usr/bin/env python3
"""
SvxLink Reflector Protocol Client Library

Python implementation of SvxLink Reflector Protocol v2.0/v3.0
Based on SvxTalk Android client and SvxLink C++ implementation.

Usage:
    from svxlink_client import SvxLinkClient

    client = SvxLinkClient("localhost", 5300)
    client.connect("MYCALL", "mypassword")
    client.select_talkgroup(1)
    client.send_talker_start()
    client.send_talker_stop()
    client.disconnect()
"""

import socket
import struct
import hmac
import hashlib
import json
import time
import threading
from typing import Optional, Callable, Dict, Any, List, Tuple
from dataclasses import dataclass
from enum import IntEnum

# Import UDP audio module
try:
    from svxlink_audio import UdpAudioChannel, check_opus_available
    UDP_AUDIO_AVAILABLE = True
except ImportError:
    UDP_AUDIO_AVAILABLE = False


class TcpMsgType(IntEnum):
    """TCP Message Types from SvxLinkProtocol"""
    HEARTBEAT = 1
    PROTO_VER = 5
    PROTO_VER_DOWNGRADE = 6
    AUTH_CHALLENGE = 10
    AUTH_RESPONSE = 11
    AUTH_OK = 12
    ERROR = 13
    SERVER_INFO = 100
    NODE_LIST = 101
    NODE_JOINED = 102
    NODE_LEFT = 103
    TALKER_START = 104
    TALKER_STOP = 105
    SELECT_TG = 106
    TG_MONITOR = 107
    REQUEST_QSY = 109
    STATE_EVENT = 110
    NODE_INFO = 111
    SIGNAL_STRENGTH = 112
    TX_STATUS = 113


class UdpMsgType(IntEnum):
    """UDP Message Types"""
    HEARTBEAT = 1
    AUDIO = 101
    FLUSH_SAMPLES = 102
    ALL_SAMPLES_FLUSHED = 103
    SIGNAL_STRENGTH = 104


class ConnectionState(IntEnum):
    """Connection state machine"""
    DISCONNECTED = 0
    CONNECTING = 1
    AUTHENTICATING = 2
    CONNECTED = 3
    ERROR = 4


@dataclass
class NodeInfo:
    """Information about a connected node"""
    callsign: str
    client_id: int = 0
    talkgroup: int = 0
    is_talker: bool = False


@dataclass
class TcpMessage:
    """Parsed TCP message"""
    msg_type: int
    payload: bytes


class SvxLinkClient:
    """
    SvxLink Reflector Protocol Client

    Implements TCP control channel with authentication.
    UDP audio channel not implemented (for testing purposes only).
    """

    # Protocol version
    PROTO_MAJOR = 2
    PROTO_MINOR = 0

    # Timeouts
    CONNECT_TIMEOUT = 10.0
    READ_TIMEOUT = 1.0  # Short timeout for responsive message handling

    def __init__(self, host: str, port: int = 5300):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.state = ConnectionState.DISCONNECTED
        self.callsign = ""
        self.client_id = 0
        self.current_talkgroup = 0
        self.nodes: List[NodeInfo] = []
        self.current_talker: Optional[str] = None
        self.last_error: str = ""

        # Callbacks
        self.on_node_list: Optional[Callable[[List[NodeInfo]], None]] = None
        self.on_node_joined: Optional[Callable[[NodeInfo], None]] = None
        self.on_node_left: Optional[Callable[[str], None]] = None
        self.on_talker_start: Optional[Callable[[str], None]] = None
        self.on_talker_stop: Optional[Callable[[str], None]] = None
        self.on_error: Optional[Callable[[str], None]] = None
        self.on_tx_status: Optional[Callable[[bool, str], None]] = None
        self.on_audio_received: Optional[Callable[[int, bytes], None]] = None

        # Receive thread
        self._recv_thread: Optional[threading.Thread] = None
        self._running = False

        # UDP Audio channel
        self._udp_audio: Optional[UdpAudioChannel] = None if not UDP_AUDIO_AVAILABLE else None

    def connect(self, callsign: str, auth_key: str,
                talkgroup: int = 1) -> Tuple[bool, str]:
        """
        Connect and authenticate to reflector

        Returns:
            Tuple of (success, message)
        """
        self.callsign = callsign.upper()
        self.current_talkgroup = talkgroup

        try:
            # Create socket
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.CONNECT_TIMEOUT)
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            self.state = ConnectionState.CONNECTING
            self.socket.connect((self.host, self.port))

            # Send protocol version
            self._send_proto_version()

            # Wait for auth challenge
            msg = self._read_message()
            if msg.msg_type != TcpMsgType.AUTH_CHALLENGE:
                self.last_error = f"Expected AUTH_CHALLENGE, got {msg.msg_type}"
                self.disconnect()
                return False, self.last_error

            self.state = ConnectionState.AUTHENTICATING

            # Parse challenge
            challenge_len = struct.unpack(">H", msg.payload[:2])[0]
            challenge = msg.payload[2:2+challenge_len]

            # Compute HMAC-SHA1 digest
            digest = hmac.new(
                auth_key.encode('utf-8'),
                challenge,
                hashlib.sha1
            ).digest()

            # Send auth response
            self._send_auth_response(callsign, digest)

            # Wait for auth result
            msg = self._read_message()

            if msg.msg_type == TcpMsgType.AUTH_OK:
                self.state = ConnectionState.CONNECTED

                # Send node info
                self._send_node_info()

                # Select talkgroup
                self._send_select_talkgroup(talkgroup)

                # Wait for SERVER_INFO to get client_id
                # (needed for UDP audio)
                # Server sends multiple messages after AUTH_OK, read until we get SERVER_INFO
                for i in range(10):  # Try up to 10 messages
                    try:
                        msg = self._read_message()
                        # DEBUG
                        # print(f"DEBUG: Got msg type {msg.msg_type}")
                        if msg.msg_type == TcpMsgType.SERVER_INFO:
                            # SERVER_INFO format: reserved(2) + clientId(2) + codecs...
                            if len(msg.payload) >= 4:
                                self.client_id = struct.unpack(
                                    ">H", msg.payload[2:4]
                                )[0]
                            break
                        # Handle other messages that might come before SERVER_INFO
                        self._handle_message(msg)
                    except socket.timeout:
                        break
                    except Exception as e:
                        # print(f"DEBUG: Exception reading msg: {e}")
                        break

                # Start receive loop
                self._running = True
                self._recv_thread = threading.Thread(
                    target=self._receive_loop,
                    daemon=True
                )
                self._recv_thread.start()

                return True, "Connected"

            elif msg.msg_type == TcpMsgType.ERROR:
                error_msg = msg.payload.decode('utf-8', errors='replace')
                self.last_error = error_msg
                self.state = ConnectionState.ERROR
                self.disconnect()
                return False, error_msg

            else:
                self.last_error = f"Unexpected response: {msg.msg_type}"
                self.disconnect()
                return False, self.last_error

        except socket.timeout:
            self.last_error = "Connection timeout"
            self.disconnect()
            return False, self.last_error
        except ConnectionRefusedError:
            self.last_error = "Connection refused"
            self.disconnect()
            return False, self.last_error
        except Exception as e:
            self.last_error = str(e)
            self.disconnect()
            return False, self.last_error

    def disconnect(self):
        """Disconnect from reflector"""
        self._running = False
        # Stop UDP audio first
        self.stop_udp_audio()
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        self.socket = None
        self.state = ConnectionState.DISCONNECTED
        self.nodes.clear()
        self.current_talker = None

    def is_connected(self) -> bool:
        """Check if connected"""
        return self.state == ConnectionState.CONNECTED

    def wait_for_disconnect(self, timeout: float = 3.0) -> bool:
        """
        Wait for the connection to be closed (e.g., after being kicked).

        Args:
            timeout: Maximum time to wait in seconds

        Returns:
            True if disconnected, False if still connected after timeout
        """
        import select

        if not self.socket:
            return True

        start = time.time()
        while time.time() - start < timeout:
            if self.state != ConnectionState.CONNECTED:
                return True

            # Use select to check if socket has data or was closed
            try:
                readable, _, _ = select.select([self.socket], [], [], 0.1)
                if readable:
                    # Try to read - if socket was closed, recv returns empty
                    try:
                        data = self.socket.recv(1, socket.MSG_PEEK)
                        if not data:
                            # Socket closed by server
                            self.state = ConnectionState.DISCONNECTED
                            return True
                    except socket.error:
                        # Socket error means disconnected
                        self.state = ConnectionState.DISCONNECTED
                        return True
            except:
                pass

            time.sleep(0.05)

        return self.state != ConnectionState.CONNECTED

    def wait_for_socket_close(self, timeout: float = 12.0) -> tuple:
        """
        Wait for the socket to be actually closed by the server.
        This verifies that the server's disconnect timer fired and closed the connection.

        Args:
            timeout: Maximum time to wait in seconds

        Returns:
            Tuple of (closed: bool, elapsed_seconds: float)
        """
        import select

        if not self.socket:
            return True, 0.0

        start = time.time()
        while time.time() - start < timeout:
            try:
                # Use select to check socket state
                readable, _, exceptional = select.select(
                    [self.socket], [], [self.socket], 0.5
                )

                if exceptional:
                    # Socket error - closed
                    elapsed = time.time() - start
                    return True, elapsed

                if readable:
                    # Try to read - empty data means closed
                    try:
                        data = self.socket.recv(1024)
                        if not data:
                            # Empty read = socket closed by remote
                            self.state = ConnectionState.DISCONNECTED
                            elapsed = time.time() - start
                            return True, elapsed
                    except socket.error:
                        elapsed = time.time() - start
                        return True, elapsed
            except Exception:
                elapsed = time.time() - start
                return True, elapsed

            time.sleep(0.1)

        return False, timeout

    def send_talker_start(self) -> bool:
        """
        Send TALKER_START (PTT press)

        Returns:
            True if sent successfully
        """
        if not self.is_connected():
            return False
        return self._send_message(TcpMsgType.TALKER_START, b"")

    def send_talker_stop(self) -> bool:
        """
        Send TALKER_STOP (PTT release)

        Returns:
            True if sent successfully
        """
        if not self.is_connected():
            return False
        return self._send_message(TcpMsgType.TALKER_STOP, b"")

    def select_talkgroup(self, tg: int) -> bool:
        """Select a talkgroup"""
        if not self.is_connected():
            return False
        self.current_talkgroup = tg
        return self._send_select_talkgroup(tg)

    def send_heartbeat(self) -> bool:
        """Send heartbeat"""
        if not self.is_connected():
            return False
        return self._send_message(TcpMsgType.HEARTBEAT, b"")

    # =========================================================================
    # UDP Audio methods
    # =========================================================================

    def start_udp_audio(self) -> bool:
        """
        Start UDP audio channel

        Must be called after connect() when client_id is available.
        Returns True if UDP audio started successfully.
        """
        if not UDP_AUDIO_AVAILABLE:
            return False
        if not self.is_connected() or self.client_id == 0:
            return False

        try:
            from svxlink_audio import UdpAudioChannel
            self._udp_audio = UdpAudioChannel(self.host, self.port)

            # Set up audio callback
            def on_audio(packet):
                if self.on_audio_received:
                    self.on_audio_received(packet.sender_id, packet.opus_data)

            self._udp_audio.on_audio_received = on_audio
            return self._udp_audio.start(self.client_id)
        except Exception as e:
            print(f"UDP audio start failed: {e}")
            return False

    def stop_udp_audio(self):
        """Stop UDP audio channel"""
        if self._udp_audio:
            self._udp_audio.stop()
            self._udp_audio = None

    def send_audio_tone(self, frequency: float = 1000.0,
                        duration_ms: int = 500) -> bool:
        """
        Send a test tone via UDP

        Args:
            frequency: Tone frequency in Hz (default 1kHz)
            duration_ms: Duration in milliseconds

        Returns:
            True if tone sent successfully
        """
        if not self._udp_audio:
            return False
        return self._udp_audio.send_tone(frequency, duration_ms)

    def send_udp_audio(self, opus_data: bytes) -> bool:
        """
        Send raw Opus audio data via UDP

        Args:
            opus_data: Opus encoded audio frame

        Returns:
            True if sent successfully
        """
        if not self._udp_audio:
            return False
        return self._udp_audio.send_audio(opus_data)

    def send_audio_flush(self) -> bool:
        """Send flush to indicate end of audio transmission"""
        if not self._udp_audio:
            return False
        return self._udp_audio.send_flush()

    def get_received_audio_count(self) -> int:
        """Get count of received audio packets"""
        if not self._udp_audio:
            return 0
        return self._udp_audio.get_received_audio_count()

    def has_received_audio_from(self, client_id: int) -> bool:
        """Check if audio was received from specific client"""
        if not self._udp_audio:
            return False
        return self._udp_audio.has_received_audio_from(client_id)

    def clear_received_audio(self):
        """Clear received audio buffer"""
        if self._udp_audio:
            self._udp_audio.clear_received()

    @staticmethod
    def is_udp_audio_available() -> bool:
        """Check if UDP audio support is available"""
        return UDP_AUDIO_AVAILABLE and check_opus_available() if UDP_AUDIO_AVAILABLE else False

    def wait_for_tx_status(self, timeout: float = 2.0) -> Tuple[bool, str]:
        """
        Wait for TX_STATUS response after TALKER_START

        Returns:
            Tuple of (allowed, message)
            - (True, "") if TX allowed
            - (False, "reason") if TX blocked (e.g., muted)
        """
        # TX_STATUS is handled in receive loop via callback
        # For synchronous testing, we check the last known state
        start = time.time()
        while time.time() - start < timeout:
            # Check if we got an error response
            if self.last_error and "mute" in self.last_error.lower():
                return False, self.last_error
            time.sleep(0.1)
        return True, ""

    # =========================================================================
    # Private methods
    # =========================================================================

    def _send_message(self, msg_type: int, payload: bytes) -> bool:
        """Send a framed message"""
        if not self.socket:
            return False
        try:
            # Frame: 4-byte length + 2-byte type + payload
            frame_len = 2 + len(payload)
            frame = struct.pack(">IH", frame_len, msg_type) + payload
            self.socket.sendall(frame)
            return True
        except Exception as e:
            self.last_error = str(e)
            return False

    def _read_message(self) -> TcpMessage:
        """Read a framed message"""
        if not self.socket:
            raise Exception("Not connected")

        # Read frame length (4 bytes)
        data = self._recv_exact(4)
        frame_len = struct.unpack(">I", data)[0]

        if frame_len < 2:
            raise Exception(f"Invalid frame length: {frame_len}")

        # Read message type (2 bytes)
        data = self._recv_exact(2)
        msg_type = struct.unpack(">H", data)[0]

        # Read payload
        payload_len = frame_len - 2
        payload = self._recv_exact(payload_len) if payload_len > 0 else b""

        return TcpMessage(msg_type, payload)

    def _recv_exact(self, n: int) -> bytes:
        """Receive exactly n bytes"""
        data = b""
        while len(data) < n:
            chunk = self.socket.recv(n - len(data))
            if not chunk:
                raise Exception("Connection closed")
            data += chunk
        return data

    def _send_proto_version(self):
        """Send PROTO_VER message"""
        payload = struct.pack(">HH", self.PROTO_MAJOR, self.PROTO_MINOR)
        self._send_message(TcpMsgType.PROTO_VER, payload)

    def _send_auth_response(self, callsign: str, digest: bytes):
        """Send AUTH_RESPONSE message"""
        callsign_bytes = callsign.encode('utf-8')
        # Format: callsign_len(2) + callsign + digest_len(2) + digest
        payload = struct.pack(">H", len(callsign_bytes))
        payload += callsign_bytes
        payload += struct.pack(">H", len(digest))
        payload += digest
        self._send_message(TcpMsgType.AUTH_RESPONSE, payload)

    def _send_node_info(self):
        """Send NODE_INFO message"""
        info = {
            "sw": "svxlink-client-test",
            "swVer": "1.0.0",
            "callsign": self.callsign
        }
        json_bytes = json.dumps(info).encode('utf-8')
        payload = struct.pack(">H", len(json_bytes)) + json_bytes
        self._send_message(TcpMsgType.NODE_INFO, payload)

    def _send_select_talkgroup(self, tg: int) -> bool:
        """Send SELECT_TG message"""
        payload = struct.pack(">I", tg)
        return self._send_message(TcpMsgType.SELECT_TG, payload)

    def _receive_loop(self):
        """Background receive loop"""
        self.socket.settimeout(self.READ_TIMEOUT)

        while self._running:
            try:
                msg = self._read_message()
                self._handle_message(msg)
            except socket.timeout:
                # Send heartbeat on timeout
                self.send_heartbeat()
            except Exception as e:
                if self._running:
                    self.last_error = str(e)
                    self.state = ConnectionState.ERROR
                    if self.on_error:
                        self.on_error(str(e))
                break

    def _handle_message(self, msg: TcpMessage):
        """Handle incoming message"""
        if msg.msg_type == TcpMsgType.HEARTBEAT:
            pass  # Ignore heartbeat

        elif msg.msg_type == TcpMsgType.SERVER_INFO:
            # Parse server info: reserved(2) + clientId(2) + codecs...
            if len(msg.payload) >= 4:
                self.client_id = struct.unpack(">H", msg.payload[2:4])[0]

        elif msg.msg_type == TcpMsgType.NODE_LIST:
            self._parse_node_list(msg.payload)

        elif msg.msg_type == TcpMsgType.NODE_JOINED:
            self._parse_node_joined(msg.payload)

        elif msg.msg_type == TcpMsgType.NODE_LEFT:
            self._parse_node_left(msg.payload)

        elif msg.msg_type == TcpMsgType.TALKER_START:
            self._parse_talker_start(msg.payload)

        elif msg.msg_type == TcpMsgType.TALKER_STOP:
            self._parse_talker_stop(msg.payload)

        elif msg.msg_type == TcpMsgType.TX_STATUS:
            self._parse_tx_status(msg.payload)

        elif msg.msg_type == TcpMsgType.ERROR:
            error_msg = msg.payload.decode('utf-8', errors='replace')
            self.last_error = error_msg
            self.state = ConnectionState.ERROR
            if self.on_error:
                self.on_error(error_msg)

    def _parse_node_list(self, payload: bytes):
        """Parse NODE_LIST message"""
        self.nodes.clear()
        offset = 0

        # Count (2 bytes)
        if len(payload) < 2:
            return
        count = struct.unpack(">H", payload[offset:offset+2])[0]
        offset += 2

        for _ in range(count):
            if offset + 4 > len(payload):
                break
            # callsign_len (2) + callsign
            cs_len = struct.unpack(">H", payload[offset:offset+2])[0]
            offset += 2
            callsign = payload[offset:offset+cs_len].decode('utf-8', errors='replace')
            offset += cs_len

            self.nodes.append(NodeInfo(callsign=callsign))

        if self.on_node_list:
            self.on_node_list(self.nodes)

    def _parse_node_joined(self, payload: bytes):
        """Parse NODE_JOINED message"""
        if len(payload) < 2:
            return
        cs_len = struct.unpack(">H", payload[:2])[0]
        callsign = payload[2:2+cs_len].decode('utf-8', errors='replace')

        node = NodeInfo(callsign=callsign)
        self.nodes.append(node)

        if self.on_node_joined:
            self.on_node_joined(node)

    def _parse_node_left(self, payload: bytes):
        """Parse NODE_LEFT message"""
        if len(payload) < 2:
            return
        cs_len = struct.unpack(">H", payload[:2])[0]
        callsign = payload[2:2+cs_len].decode('utf-8', errors='replace')

        self.nodes = [n for n in self.nodes if n.callsign != callsign]

        if self.on_node_left:
            self.on_node_left(callsign)

    def _parse_talker_start(self, payload: bytes):
        """Parse TALKER_START message (v2 format: tg(4) + callsign_len(2) + callsign)"""
        # Protocol v2+ has talkgroup first
        if len(payload) < 6:
            return

        # talkgroup (4 bytes) + callsign_len (2 bytes) + callsign
        tg = struct.unpack(">I", payload[:4])[0]
        cs_len = struct.unpack(">H", payload[4:6])[0]
        callsign = payload[6:6+cs_len].decode('utf-8', errors='replace')

        self.current_talker = callsign

        if self.on_talker_start:
            self.on_talker_start(callsign)

    def _parse_talker_stop(self, payload: bytes):
        """Parse TALKER_STOP message (v2 format: tg(4) + callsign_len(2) + callsign)"""
        if len(payload) < 6:
            callsign = self.current_talker or ""
        else:
            # talkgroup (4 bytes) + callsign_len (2 bytes) + callsign
            tg = struct.unpack(">I", payload[:4])[0]
            cs_len = struct.unpack(">H", payload[4:6])[0]
            callsign = payload[6:6+cs_len].decode('utf-8', errors='replace')

        self.current_talker = None

        if self.on_talker_stop:
            self.on_talker_stop(callsign)

    def _parse_tx_status(self, payload: bytes):
        """Parse TX_STATUS message"""
        if len(payload) < 1:
            return

        # First byte: 1 = allowed, 0 = blocked
        allowed = payload[0] == 1
        reason = ""

        if len(payload) > 1:
            reason_len = struct.unpack(">H", payload[1:3])[0]
            reason = payload[3:3+reason_len].decode('utf-8', errors='replace')

        if not allowed:
            self.last_error = reason or "TX blocked"

        if self.on_tx_status:
            self.on_tx_status(allowed, reason)


# Convenience functions for testing
def quick_connect(host: str, port: int, callsign: str,
                  password: str) -> Tuple[bool, str, Optional[SvxLinkClient]]:
    """
    Quick connect helper for testing

    Returns:
        Tuple of (success, message, client or None)
    """
    client = SvxLinkClient(host, port)
    success, msg = client.connect(callsign, password)
    if success:
        return True, msg, client
    return False, msg, None


if __name__ == "__main__":
    # Simple test
    import sys

    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 5300
    callsign = sys.argv[3] if len(sys.argv) > 3 else "TEST"
    password = sys.argv[4] if len(sys.argv) > 4 else "test123"

    print(f"Connecting to {host}:{port} as {callsign}...")

    client = SvxLinkClient(host, port)
    success, msg = client.connect(callsign, password)

    if success:
        print(f"Connected! Client ID: {client.client_id}")
        print("Press Ctrl+C to disconnect...")
        try:
            while client.is_connected():
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        client.disconnect()
        print("Disconnected")
    else:
        print(f"Failed: {msg}")
        sys.exit(1)
