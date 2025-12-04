#!/usr/bin/env python3
"""
SvxLink Reflector UDP Audio Module

Provides UDP audio channel support for SvxLink Reflector Protocol.
Uses opuslib_next for Opus encoding/decoding and generates 1kHz test tones.

UDP Message Format (Protocol V2):
- type: uint16_t (2 bytes, big-endian)
- clientId: uint16_t (2 bytes)
- seqNum: uint16_t (2 bytes)
- [for audio] audioLen: uint16_t (2 bytes) + audioData (variable)

Audio Format:
- Sample rate: 16000 Hz (SvxLink internal rate)
- Channels: 1 (mono)
- Opus frame size: 20ms = 320 samples
"""

import socket
import struct
import threading
import time
import math
from typing import Optional, Callable, List
from dataclasses import dataclass
from enum import IntEnum

# Try to import opuslib_next
try:
    import opuslib_next as opuslib
    OPUS_AVAILABLE = True
except ImportError:
    try:
        import opuslib
        OPUS_AVAILABLE = True
    except ImportError:
        OPUS_AVAILABLE = False
        print("Warning: opuslib_next not available, audio encoding disabled")


class UdpMsgType(IntEnum):
    """UDP Message Types"""
    HEARTBEAT = 1
    AUDIO = 101
    FLUSH_SAMPLES = 102
    ALL_SAMPLES_FLUSHED = 103
    SIGNAL_STRENGTH = 104


@dataclass
class AudioPacket:
    """Received audio packet"""
    sender_id: int
    seq_num: int
    opus_data: bytes
    is_flush: bool = False


class ToneGenerator:
    """Generate sine wave test tones"""

    def __init__(self, sample_rate: int = 16000):
        self.sample_rate = sample_rate

    def generate_tone(self, frequency: float, duration_ms: int) -> bytes:
        """
        Generate a sine wave tone

        Args:
            frequency: Tone frequency in Hz (e.g., 1000 for 1kHz)
            duration_ms: Duration in milliseconds

        Returns:
            PCM audio data as bytes (16-bit signed, little-endian)
        """
        num_samples = int(self.sample_rate * duration_ms / 1000)
        samples = []

        for i in range(num_samples):
            t = i / self.sample_rate
            # Generate sine wave with amplitude 0.8 to avoid clipping
            value = int(0.8 * 32767 * math.sin(2 * math.pi * frequency * t))
            samples.append(value)

        # Pack as 16-bit signed little-endian
        return struct.pack(f'<{len(samples)}h', *samples)

    def generate_silence(self, duration_ms: int) -> bytes:
        """Generate silence"""
        num_samples = int(self.sample_rate * duration_ms / 1000)
        return b'\x00\x00' * num_samples


class OpusCodec:
    """Opus encoder/decoder wrapper"""

    SAMPLE_RATE = 16000  # SvxLink internal rate
    CHANNELS = 1
    FRAME_SIZE_MS = 20   # 20ms frames
    FRAME_SIZE = SAMPLE_RATE * FRAME_SIZE_MS // 1000  # 320 samples

    def __init__(self):
        if not OPUS_AVAILABLE:
            raise RuntimeError("opuslib_next not installed")

        self.encoder = opuslib.Encoder(
            self.SAMPLE_RATE,
            self.CHANNELS,
            opuslib.APPLICATION_VOIP
        )
        self.decoder = opuslib.Decoder(
            self.SAMPLE_RATE,
            self.CHANNELS
        )

    def encode(self, pcm_data: bytes) -> List[bytes]:
        """
        Encode PCM audio to Opus frames

        Args:
            pcm_data: Raw PCM audio (16-bit signed, little-endian)

        Returns:
            List of Opus encoded frames
        """
        frames = []
        frame_bytes = self.FRAME_SIZE * 2  # 2 bytes per sample

        for i in range(0, len(pcm_data), frame_bytes):
            frame = pcm_data[i:i + frame_bytes]
            if len(frame) < frame_bytes:
                # Pad with silence
                frame += b'\x00' * (frame_bytes - len(frame))

            encoded = self.encoder.encode(frame, self.FRAME_SIZE)
            frames.append(encoded)

        return frames

    def decode(self, opus_data: bytes) -> bytes:
        """
        Decode Opus frame to PCM audio

        Args:
            opus_data: Opus encoded frame

        Returns:
            Raw PCM audio (16-bit signed, little-endian)
        """
        return self.decoder.decode(opus_data, self.FRAME_SIZE)


class UdpAudioChannel:
    """
    UDP Audio Channel for SvxLink Reflector

    Handles UDP audio transmission and reception with Opus encoding.
    """

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.client_id = 0
        self.seq_num = 0

        self._running = False
        self._recv_thread: Optional[threading.Thread] = None

        # Callbacks
        self.on_audio_received: Optional[Callable[[AudioPacket], None]] = None
        self.on_flush_received: Optional[Callable[[int], None]] = None

        # Audio codec
        self._codec: Optional[OpusCodec] = None
        self._tone_gen = ToneGenerator()

        # Received audio buffer
        self.received_packets: List[AudioPacket] = []

    def start(self, client_id: int) -> bool:
        """
        Start UDP audio channel

        Args:
            client_id: Client ID from SERVER_INFO TCP message

        Returns:
            True if started successfully
        """
        try:
            self.client_id = client_id
            self.seq_num = 0

            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.settimeout(1.0)

            # Initialize codec if available
            if OPUS_AVAILABLE:
                try:
                    self._codec = OpusCodec()
                except Exception as e:
                    print(f"Warning: Opus codec init failed: {e}")
                    self._codec = None

            # Send initial heartbeat to register UDP port with server
            self.send_heartbeat()

            # Start receive loop
            self._running = True
            self._recv_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self._recv_thread.start()

            return True

        except Exception as e:
            print(f"UDP start failed: {e}")
            return False

    def stop(self):
        """Stop UDP audio channel"""
        self._running = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        self.socket = None

    def send_heartbeat(self) -> bool:
        """Send UDP heartbeat"""
        if not self.socket or self.client_id == 0:
            return False

        # Format: type(2) + clientId(2) + seqNum(2)
        packet = struct.pack('>HHH',
            UdpMsgType.HEARTBEAT,
            self.client_id,
            self.seq_num
        )
        self.seq_num = (self.seq_num + 1) & 0xFFFF
        return self._send_packet(packet)

    def send_audio(self, opus_data: bytes) -> bool:
        """
        Send audio packet

        Args:
            opus_data: Opus encoded audio data

        Returns:
            True if sent successfully
        """
        if not self.socket or self.client_id == 0:
            return False

        # Format: type(2) + clientId(2) + seqNum(2) + audioLen(2) + audioData
        packet = struct.pack('>HHHH',
            UdpMsgType.AUDIO,
            self.client_id,
            self.seq_num,
            len(opus_data)
        ) + opus_data
        self.seq_num = (self.seq_num + 1) & 0xFFFF
        return self._send_packet(packet)

    def send_flush(self) -> bool:
        """Send flush samples notification (end of transmission)"""
        if not self.socket or self.client_id == 0:
            return False

        packet = struct.pack('>HHH',
            UdpMsgType.FLUSH_SAMPLES,
            self.client_id,
            self.seq_num
        )
        self.seq_num = (self.seq_num + 1) & 0xFFFF
        return self._send_packet(packet)

    def send_all_samples_flushed(self) -> bool:
        """Send acknowledgment that all samples have been flushed"""
        if not self.socket or self.client_id == 0:
            return False

        packet = struct.pack('>HHH',
            UdpMsgType.ALL_SAMPLES_FLUSHED,
            self.client_id,
            self.seq_num
        )
        self.seq_num = (self.seq_num + 1) & 0xFFFF
        return self._send_packet(packet)

    def send_tone(self, frequency: float = 1000.0, duration_ms: int = 500) -> bool:
        """
        Send a test tone

        Args:
            frequency: Tone frequency in Hz (default 1kHz)
            duration_ms: Duration in milliseconds

        Returns:
            True if all frames sent successfully
        """
        if not self._codec:
            print("Warning: Opus codec not available, cannot send tone")
            return False

        # Generate tone
        pcm_data = self._tone_gen.generate_tone(frequency, duration_ms)

        # Encode to Opus frames
        frames = self._codec.encode(pcm_data)

        # Send each frame
        for frame in frames:
            if not self.send_audio(frame):
                return False
            # Small delay between frames (20ms frame = ~50 fps)
            time.sleep(0.015)

        # Send flush to indicate end of audio
        self.send_flush()
        return True

    def _send_packet(self, data: bytes) -> bool:
        """Send UDP packet to server"""
        if not self.socket:
            return False
        try:
            self.socket.sendto(data, (self.host, self.port))
            return True
        except Exception as e:
            print(f"UDP send failed: {e}")
            return False

    def _receive_loop(self):
        """Background receive loop"""
        while self._running and self.socket:
            try:
                data, addr = self.socket.recvfrom(2048)
                self._process_packet(data)
            except socket.timeout:
                # Send heartbeat on timeout
                self.send_heartbeat()
            except Exception as e:
                if self._running:
                    print(f"UDP receive error: {e}")
                break

    def _process_packet(self, data: bytes):
        """Process received UDP packet"""
        # Minimum header: type(2) + clientId(2) + seqNum(2) = 6 bytes
        if len(data) < 6:
            return

        msg_type, sender_id, seq_num = struct.unpack('>HHH', data[:6])

        if msg_type == UdpMsgType.HEARTBEAT:
            # Heartbeat received
            pass

        elif msg_type == UdpMsgType.AUDIO:
            # Audio packet: header(6) + audioLen(2) + audioData
            if len(data) < 8:
                return
            audio_len = struct.unpack('>H', data[6:8])[0]
            if len(data) < 8 + audio_len:
                return
            audio_data = data[8:8 + audio_len]

            packet = AudioPacket(sender_id, seq_num, audio_data)
            self.received_packets.append(packet)

            if self.on_audio_received:
                self.on_audio_received(packet)

        elif msg_type == UdpMsgType.FLUSH_SAMPLES:
            packet = AudioPacket(sender_id, seq_num, b'', is_flush=True)
            self.received_packets.append(packet)

            if self.on_flush_received:
                self.on_flush_received(sender_id)

            # Send acknowledgment
            self.send_all_samples_flushed()

        elif msg_type == UdpMsgType.ALL_SAMPLES_FLUSHED:
            # Acknowledgment received
            pass

    def clear_received(self):
        """Clear received packets buffer"""
        self.received_packets.clear()

    def get_received_audio_count(self) -> int:
        """Get count of received audio packets (excluding flush)"""
        return len([p for p in self.received_packets if not p.is_flush])

    def has_received_audio_from(self, sender_id: int) -> bool:
        """Check if audio was received from specific sender"""
        return any(p.sender_id == sender_id and not p.is_flush
                   for p in self.received_packets)


def check_opus_available() -> bool:
    """Check if Opus codec is available"""
    return OPUS_AVAILABLE


if __name__ == "__main__":
    # Quick test
    print(f"Opus available: {OPUS_AVAILABLE}")

    if OPUS_AVAILABLE:
        # Test tone generation and encoding
        tone_gen = ToneGenerator()
        pcm = tone_gen.generate_tone(1000, 100)  # 100ms of 1kHz
        print(f"Generated {len(pcm)} bytes of PCM audio")

        codec = OpusCodec()
        frames = codec.encode(pcm)
        print(f"Encoded to {len(frames)} Opus frames")

        # Test decode
        decoded = codec.decode(frames[0])
        print(f"Decoded frame: {len(decoded)} bytes")
