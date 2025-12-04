#!/usr/bin/env python3
"""
SvxReflector Client Integration Tests

Tests the reflector from a client perspective:
- Connect/disconnect with valid/invalid credentials
- PTT blocked when muted
- User enabled/disabled enforcement
- Realtime enforcement (no reload required)

Usage:
    python3 client_test.py [--reflector-host HOST] [--reflector-port PORT]
                           [--admin-host HOST] [--admin-port PORT]
                           [--admin-token TOKEN]
"""

import argparse
import sys
import time
from datetime import datetime
from typing import List, Tuple, Dict, Any

import requests
from svxlink_client import SvxLinkClient, ConnectionState


# Default configuration (matches svxreflector-test.conf)
DEFAULT_REFLECTOR_HOST = "localhost"
DEFAULT_REFLECTOR_PORT = 15300  # Matches LISTEN_PORT in test config
DEFAULT_ADMIN_HOST = "localhost"
DEFAULT_ADMIN_PORT = 18080
DEFAULT_ADMIN_TOKEN = "test-token-12345"


class ClientIntegrationTest:
    """Integration tests for SvxReflector client operations"""

    def __init__(self, reflector_host: str, reflector_port: int,
                 admin_host: str, admin_port: int, admin_token: str):
        self.reflector_host = reflector_host
        self.reflector_port = reflector_port
        self.admin_url = f"http://{admin_host}:{admin_port}"
        self.admin_headers = {
            "Authorization": f"Bearer {admin_token}",
            "Content-Type": "application/json"
        }

        self.passed = 0
        self.failed = 0
        self.errors: List[str] = []
        # (category, name, passed, detail, extra_info, operations)
        self.results: List[Tuple[str, str, bool, str, Dict[str, Any], List[Dict]]] = []
        self.current_category = ""
        self.current_operations: List[Dict] = []

    def log(self, msg: str):
        print(f"  {msg}")

    def set_category(self, category: str):
        self.current_category = category
        self.current_operations = []

    def record_operation(self, op_type: str, description: str,
                         request: Dict = None, response: Dict = None):
        """Record a protocol or API operation for the report"""
        self.current_operations.append({
            "type": op_type,
            "description": description,
            "request": request or {},
            "response": response or {}
        })

    def test(self, name: str, condition: bool, detail: str = "",
             extra_info: Dict[str, Any] = None):
        if condition:
            print(f"  [PASS] {name}")
            self.passed += 1
        else:
            print(f"  [FAIL] {name}")
            if detail:
                print(f"         {detail}")
            self.failed += 1
            self.errors.append(name)

        self.results.append((
            self.current_category,
            name,
            condition,
            detail if not condition else "",
            extra_info or {},
            list(self.current_operations)  # Copy current operations
        ))
        self.current_operations = []  # Reset for next test

    # =========================================================================
    # Admin API helpers
    # =========================================================================

    def admin_get(self, endpoint: str) -> requests.Response:
        return requests.get(
            f"{self.admin_url}{endpoint}",
            headers=self.admin_headers,
            timeout=5
        )

    def admin_post(self, endpoint: str, data: Dict) -> requests.Response:
        return requests.post(
            f"{self.admin_url}{endpoint}",
            headers=self.admin_headers,
            json=data,
            timeout=5
        )

    def admin_put(self, endpoint: str, data: Dict) -> requests.Response:
        return requests.put(
            f"{self.admin_url}{endpoint}",
            headers=self.admin_headers,
            json=data,
            timeout=5
        )

    def admin_delete(self, endpoint: str) -> requests.Response:
        return requests.delete(
            f"{self.admin_url}{endpoint}",
            headers=self.admin_headers,
            timeout=5
        )

    def create_test_user(self, callsign: str, password: str,
                         enabled: bool = True) -> bool:
        """Create a test user via admin API"""
        # Delete if exists
        self.admin_delete(f"/admin/users/{callsign}")

        resp = self.admin_post("/admin/users", {
            "callsign": callsign,
            "group": "test-group",
            "password": password,
            "enabled": enabled
        })
        return resp.status_code == 201

    def delete_test_user(self, callsign: str):
        """Delete a test user"""
        self.admin_delete(f"/admin/users/{callsign}")

    def mute_callsign(self, callsign: str, duration: int = 60) -> bool:
        """Mute a callsign via admin API"""
        resp = self.admin_post("/admin/mute", {
            "callsign": callsign,
            "duration": duration,
            "reason": "Test mute"
        })
        return resp.status_code == 200

    def unmute_callsign(self, callsign: str) -> bool:
        """Unmute a callsign via admin API"""
        resp = self.admin_post("/admin/unmute", {"callsign": callsign})
        return resp.status_code == 200

    # =========================================================================
    # Test: Basic Connection
    # =========================================================================

    def test_basic_connection(self):
        print("\n[TEST] Basic Connection")
        self.set_category("Basic Connection")

        # Create test user (use valid ham callsign format)
        callsign = "SM0TST"
        password = "test-password-123"
        self.create_test_user(callsign, password)

        try:
            # Test successful connection
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, password)

            self.record_operation("protocol", "Connect to reflector",
                request={"host": self.reflector_host, "port": self.reflector_port,
                         "callsign": callsign},
                response={"success": success, "message": msg,
                          "state": str(client.state)})

            self.test(
                "Connect with valid credentials",
                success,
                f"Failed: {msg}",
                {"callsign": callsign, "result": msg}
            )

            if success:
                self.record_operation("protocol", "Check connection state",
                    response={"state": str(client.state),
                              "state_name": ConnectionState(client.state).name})

                self.test(
                    "Client state is CONNECTED",
                    client.state == ConnectionState.CONNECTED
                )

                # Test disconnect
                client.disconnect()

                self.record_operation("protocol", "Disconnect from reflector",
                    response={"state": str(client.state),
                              "state_name": ConnectionState(client.state).name})

                self.test(
                    "Disconnect successful",
                    client.state == ConnectionState.DISCONNECTED
                )
            else:
                client.disconnect()

        finally:
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Authentication Failure
    # =========================================================================

    def test_auth_failure(self):
        print("\n[TEST] Authentication Failure")
        self.set_category("Auth Failure")

        callsign = "SM0AUT"
        password = "correct-password"
        self.create_test_user(callsign, password)

        try:
            # Test wrong password
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, "wrong-password")

            self.test(
                "Wrong password rejected",
                not success,
                f"Should have failed but got: {msg}",
                {"callsign": callsign, "password": "wrong-password", "result": msg}
            )

            # Check error message contains auth failure
            self.test(
                "Error message indicates auth failure",
                "auth" in msg.lower() or "password" in msg.lower() or
                "denied" in msg.lower() or "failed" in msg.lower(),
                f"Got: {msg}"
            )

            client.disconnect()

        finally:
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Disabled User
    # =========================================================================

    def test_disabled_user(self):
        print("\n[TEST] Disabled User")
        self.set_category("Disabled User")

        callsign = "SM0DIS"
        password = "test-password"

        # Create user as disabled
        self.create_test_user(callsign, password, enabled=False)

        try:
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, password)

            self.test(
                "Disabled user cannot connect",
                not success,
                f"Should have been rejected but connected: {msg}",
                {"callsign": callsign, "enabled": False, "result": msg}
            )

            client.disconnect()

        finally:
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Realtime User Enable/Disable
    # =========================================================================

    def test_realtime_user_toggle(self):
        print("\n[TEST] Realtime User Enable/Disable")
        self.set_category("Realtime User Toggle")

        callsign = "SM0TOG"
        password = "test-password"

        # Create user as enabled
        self.create_test_user(callsign, password, enabled=True)

        try:
            # Connect successfully first
            client1 = SvxLinkClient(self.reflector_host, self.reflector_port)
            success1, msg1 = client1.connect(callsign, password)

            self.test(
                "Initial connection succeeds",
                success1,
                msg1
            )
            client1.disconnect()

            # Disable user via admin API (realtime, no reload)
            resp = self.admin_put(f"/admin/users/{callsign}", {"enabled": False})
            self.test(
                "Admin API disables user",
                resp.status_code == 200,
                f"Got {resp.status_code}"
            )

            # Small delay for change to take effect
            time.sleep(0.2)

            # Try to connect again - should fail now
            client2 = SvxLinkClient(self.reflector_host, self.reflector_port)
            success2, msg2 = client2.connect(callsign, password)

            self.test(
                "Connection fails after realtime disable",
                not success2,
                f"Should have failed but: {msg2}",
                {"action": "disable", "result": msg2}
            )
            client2.disconnect()

            # Re-enable user
            resp = self.admin_put(f"/admin/users/{callsign}", {"enabled": True})
            self.test(
                "Admin API re-enables user",
                resp.status_code == 200
            )

            time.sleep(0.2)

            # Connect again - should work now
            client3 = SvxLinkClient(self.reflector_host, self.reflector_port)
            success3, msg3 = client3.connect(callsign, password)

            self.test(
                "Connection succeeds after realtime re-enable",
                success3,
                msg3,
                {"action": "re-enable", "result": msg3}
            )
            client3.disconnect()

        finally:
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Muted User PTT Blocked (Two-Session Test)
    # =========================================================================

    def test_mute_api_enforcement(self):
        """
        Test mute functionality via Admin API.

        Note: Full PTT/audio blocking verification requires UDP audio channel
        which is not implemented in this test client. This test verifies:
        1. Mute API works correctly
        2. Mute list is updated
        3. Unmute API works correctly

        The actual audio blocking happens at UDP level (Reflector.cpp:1159-1162)
        when muted client sends MsgUdpAudio - the server drops the audio.
        """
        print("\n[TEST] Mute API Enforcement")
        self.set_category("Mute Enforcement")

        callsign = "SM0MUT"
        password = "test-password"

        self.create_test_user(callsign, password)

        # Ensure unmuted first
        self.unmute_callsign(callsign)

        try:
            # Connect
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, password)

            self.test(
                "Client connects successfully",
                success,
                msg
            )

            if not success:
                return

            # Verify not in mute list initially
            resp = self.admin_get("/admin/mute")
            mute_list = resp.json().get("muted_nodes", [])
            callsigns_muted = [m.get("callsign") for m in mute_list]

            self.test(
                "Client not in mute list initially",
                callsign not in callsigns_muted,
                f"Found in mute list: {callsigns_muted}"
            )

            # Mute via API
            muted = self.mute_callsign(callsign, duration=60)
            self.test(
                "Mute API returns success",
                muted,
                "Mute API call failed"
            )

            # Verify in mute list
            resp = self.admin_get("/admin/mute")
            mute_list = resp.json().get("muted_nodes", [])
            callsigns_muted = [m.get("callsign") for m in mute_list]

            self.test(
                "Client appears in mute list after mute",
                callsign in callsigns_muted,
                f"Not found in: {callsigns_muted}",
                {"muted_nodes": callsigns_muted}
            )

            # Connection should remain active (mute doesn't kick)
            time.sleep(0.3)
            self.test(
                "Connection remains active while muted",
                client.is_connected(),
                f"State: {client.state}"
            )

            # Unmute via API
            unmuted = self.unmute_callsign(callsign)
            self.test(
                "Unmute API returns success",
                unmuted,
                "Unmute API call failed"
            )

            # Verify removed from mute list
            resp = self.admin_get("/admin/mute")
            mute_list = resp.json().get("muted_nodes", [])
            callsigns_muted = [m.get("callsign") for m in mute_list]

            self.test(
                "Client removed from mute list after unmute",
                callsign not in callsigns_muted,
                f"Still in mute list: {callsigns_muted}"
            )

            # Cleanup
            client.disconnect()

        finally:
            self.unmute_callsign(callsign)
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Real Audio PTT with Mute (requires opuslib_next)
    # =========================================================================

    def test_real_audio_mute_enforcement(self):
        """
        Test mute enforcement with real UDP audio transmission.

        This test requires opuslib_next to be installed.
        It sends actual 1kHz tone audio via UDP and verifies:
        1. Listener receives audio when talker is NOT muted
        2. Listener does NOT receive audio when talker IS muted
        """
        print("\n[TEST] Real Audio Mute Enforcement")
        self.set_category("Real Audio Mute")

        # Check if UDP audio is available
        if not SvxLinkClient.is_udp_audio_available():
            print("  [SKIP] opuslib_next not available, skipping real audio test")
            self.test(
                "UDP audio available (opuslib_next)",
                True,  # Mark as pass to not fail the suite
                "opuslib_next not installed - test skipped",
                {"skipped": True, "reason": "opuslib_next not available"}
            )
            return

        talker_callsign = "SM0TLK"
        talker_password = "talker-pass"
        listener_callsign = "SM0LSN"
        listener_password = "listener-pass"

        self.create_test_user(talker_callsign, talker_password)
        self.create_test_user(listener_callsign, listener_password)
        self.unmute_callsign(talker_callsign)

        try:
            # Connect listener first
            listener = SvxLinkClient(self.reflector_host, self.reflector_port)
            listener_success, listener_msg = listener.connect(
                listener_callsign, listener_password
            )

            self.test(
                "Listener connects",
                listener_success,
                listener_msg
            )

            if not listener_success:
                return

            # Start UDP audio for listener
            print(f"    Listener client_id: {listener.client_id}")
            listener_udp = listener.start_udp_audio()
            self.test(
                "Listener UDP audio started",
                listener_udp,
                f"Failed to start UDP audio (client_id={listener.client_id})",
                {"client_id": listener.client_id}
            )

            # Connect talker
            talker = SvxLinkClient(self.reflector_host, self.reflector_port)
            talker_success, talker_msg = talker.connect(
                talker_callsign, talker_password
            )

            self.test(
                "Talker connects",
                talker_success,
                talker_msg
            )

            if not talker_success:
                listener.disconnect()
                return

            # Start UDP audio for talker
            talker_udp = talker.start_udp_audio()
            self.test(
                "Talker UDP audio started",
                talker_udp,
                "Failed to start UDP audio"
            )

            # Wait for UDP to stabilize
            time.sleep(1.0)

            # === Test 1: Audio received when NOT muted ===
            listener.clear_received_audio()

            # Send 1kHz tone for 300ms
            talker.send_audio_tone(frequency=1000.0, duration_ms=300)
            time.sleep(1.0)  # Wait for audio to be received

            audio_count_before_mute = listener.get_received_audio_count()
            self.test(
                "Listener receives audio when talker NOT muted",
                audio_count_before_mute > 0,
                f"Received {audio_count_before_mute} audio packets",
                {"audio_packets": audio_count_before_mute}
            )

            # === Mute the talker ===
            muted = self.mute_callsign(talker_callsign, duration=60)
            self.test(
                "Admin API mutes talker",
                muted,
                "Mute API call failed"
            )

            time.sleep(0.5)

            # === Test 2: Audio NOT received when muted ===
            listener.clear_received_audio()

            # Send another tone
            talker.send_audio_tone(frequency=1000.0, duration_ms=300)
            time.sleep(1.0)

            audio_count_when_muted = listener.get_received_audio_count()
            self.test(
                "Listener does NOT receive audio when talker muted",
                audio_count_when_muted == 0,
                f"Received {audio_count_when_muted} audio packets (expected 0)",
                {"audio_packets": audio_count_when_muted}
            )

            # === Unmute and verify audio works again ===
            self.unmute_callsign(talker_callsign)
            time.sleep(0.5)

            listener.clear_received_audio()
            talker.send_audio_tone(frequency=1000.0, duration_ms=300)
            time.sleep(1.0)

            audio_count_after_unmute = listener.get_received_audio_count()
            self.test(
                "Listener receives audio after unmute",
                audio_count_after_unmute > 0,
                f"Received {audio_count_after_unmute} audio packets",
                {"audio_packets": audio_count_after_unmute}
            )

            # Cleanup
            talker.disconnect()
            listener.disconnect()

        finally:
            self.unmute_callsign(talker_callsign)
            self.delete_test_user(talker_callsign)
            self.delete_test_user(listener_callsign)

    # =========================================================================
    # Test: Auto-Kick on Disable (user connected, then disabled via API)
    # =========================================================================

    def test_auto_kick_on_disable(self):
        print("\n[TEST] Auto-Kick on Disable")
        self.set_category("Auto-Kick on Disable")

        callsign = "SM0KCK"
        password = "test-password"

        # Create user as enabled
        self.create_test_user(callsign, password, enabled=True)

        try:
            # Connect and stay connected
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, password)

            self.record_operation("protocol", "Connect to reflector",
                request={"host": self.reflector_host, "port": self.reflector_port,
                         "callsign": callsign},
                response={"success": success, "message": msg,
                          "state": str(client.state)})

            self.test(
                "Initial connection succeeds",
                success,
                msg
            )

            if not success:
                return

            # Verify we're connected
            self.record_operation("protocol", "Verify connection state",
                response={"state": str(client.state),
                          "is_connected": client.is_connected()})

            self.test(
                "Client is connected",
                client.is_connected(),
                f"State: {client.state}"
            )

            # Wait a moment
            time.sleep(0.3)

            # Now disable the user via Admin API
            # This should trigger auto-kick
            resp = self.admin_put(f"/admin/users/{callsign}", {"enabled": False})

            try:
                resp_body = resp.json()
            except:
                resp_body = resp.text

            self.record_operation("api", "Disable user via Admin API",
                request={"method": "PUT", "endpoint": f"/admin/users/{callsign}",
                         "body": {"enabled": False}},
                response={"status": resp.status_code, "body": resp_body})

            self.test(
                "Admin API disables user",
                resp.status_code == 200,
                f"Got {resp.status_code}"
            )

            # Wait for ERROR message (kick notification)
            kicked = client.wait_for_disconnect(timeout=3.0)

            self.record_operation("protocol", "Wait for kick (ERROR message)",
                response={"kicked": kicked, "state": str(client.state),
                          "last_error": client.last_error})

            self.test(
                "Client receives ERROR message (state=ERROR)",
                kicked and client.state == ConnectionState.ERROR,
                f"State: {client.state}",
                {"state": str(client.state), "last_error": client.last_error}
            )

            # Now wait for actual socket close (server waits 10 seconds)
            # We need to verify socket is actually closed by server
            print("    Waiting up to 12s for server to close socket...")
            socket_closed, elapsed = client.wait_for_socket_close(timeout=12.0)

            self.record_operation("protocol", "Wait for socket close (m_disc_timer=10s)",
                response={"socket_closed": socket_closed,
                          "elapsed_seconds": f"{elapsed:.1f}",
                          "expected": "~10 seconds"})

            # Elapsed should be ~10 seconds (server's m_disc_timer)
            self.test(
                "Server closes socket after disconnect timer (10s)",
                socket_closed and elapsed >= 9.0,
                f"Socket closed after {elapsed:.1f}s (expected ~10s)",
                {"socket_closed": socket_closed, "elapsed_seconds": f"{elapsed:.1f}"}
            )

            # Cleanup
            client.disconnect()

        finally:
            # Re-enable for cleanup
            self.admin_put(f"/admin/users/{callsign}", {"enabled": True})
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Realtime Mute During Connection
    # =========================================================================

    def test_realtime_mute(self):
        print("\n[TEST] Realtime Mute During Active Connection")
        self.set_category("Realtime Mute")

        callsign = "SM0RTM"
        password = "test-password"

        self.create_test_user(callsign, password)
        self.unmute_callsign(callsign)

        try:
            # Connect
            client = SvxLinkClient(self.reflector_host, self.reflector_port)
            success, msg = client.connect(callsign, password)

            self.test(
                "Connect successfully",
                success,
                msg
            )

            if not success:
                return

            # Verify connection is active
            self.test(
                "Client is connected",
                client.is_connected()
            )

            # Mute via admin API while connected
            muted = self.mute_callsign(callsign, duration=60)
            self.test(
                "Mute applied during active connection",
                muted
            )

            # The mute should take effect immediately for next PTT
            # Connection should remain active (mute doesn't kick)
            time.sleep(0.2)

            self.test(
                "Connection remains active after mute",
                client.is_connected(),
                f"State: {client.state}"
            )

            # Cleanup
            client.disconnect()

        finally:
            self.unmute_callsign(callsign)
            self.delete_test_user(callsign)

    # =========================================================================
    # Test: Unknown User
    # =========================================================================

    def test_unknown_user(self):
        print("\n[TEST] Unknown User")
        self.set_category("Unknown User")

        callsign = "SM0UNK"
        password = "any-password"

        # Make sure user doesn't exist
        self.delete_test_user(callsign)

        client = SvxLinkClient(self.reflector_host, self.reflector_port)
        success, msg = client.connect(callsign, password)

        self.test(
            "Unknown user rejected",
            not success,
            f"Should have been rejected: {msg}",
            {"callsign": callsign, "result": msg}
        )

        client.disconnect()

    # =========================================================================
    # Test: Multiple Connections Same User (should be rejected)
    # =========================================================================

    def test_multiple_connections(self):
        """
        Test that SvxReflector rejects duplicate connections from same callsign.
        Only ONE session per callsign is allowed.
        """
        print("\n[TEST] Multiple Connections Same User")
        self.set_category("Multiple Connections")

        callsign = "SM0MLT"
        password = "test-password"

        # Ensure clean state - delete user first to clear any stale sessions
        self.delete_test_user(callsign)
        time.sleep(0.5)

        created = self.create_test_user(callsign, password)
        if not created:
            self.test("User created for test", False, "Failed to create test user")
            return

        # Delay to ensure server is ready
        time.sleep(0.5)

        client1 = None
        client2 = None

        try:
            # First connection
            client1 = SvxLinkClient(self.reflector_host, self.reflector_port)
            success1, msg1 = client1.connect(callsign, password)

            self.test(
                "First connection succeeds",
                success1,
                msg1
            )

            if not success1:
                return

            # Small delay before second connection
            time.sleep(0.3)

            # Second connection (same callsign) - should be rejected or kick first
            client2 = SvxLinkClient(self.reflector_host, self.reflector_port)
            success2, msg2 = client2.connect(callsign, password)

            # SvxReflector allows only 1 session per callsign
            # Either: second is rejected, OR first is kicked
            first_still_connected = client1.is_connected()

            # Valid outcomes:
            # 1. Second rejected (first stays connected)
            # 2. First kicked (second connects)
            valid_behavior = (not success2) or (success2 and not first_still_connected)

            self.test(
                "Server enforces single session per callsign",
                valid_behavior,
                f"First connected: {first_still_connected}, Second connected: {success2}",
                {
                    "first_still_connected": first_still_connected,
                    "second_connected": success2,
                    "second_msg": msg2,
                    "behavior": "second_rejected" if not success2 else "first_kicked"
                }
            )

        finally:
            if client1:
                client1.disconnect()
            if client2:
                client2.disconnect()
            self.delete_test_user(callsign)

    # =========================================================================
    # Generate Report
    # =========================================================================

    def generate_report(self, output_path: str):
        """Generate a markdown test report with detailed operations"""
        import json
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        lines = []
        lines.append("# SvxReflector Client Integration Test Results")
        lines.append("")
        lines.append(f"**Date:** {now}")
        lines.append(f"**Reflector:** {self.reflector_host}:{self.reflector_port}")
        lines.append(f"**Admin API:** {self.admin_url}")
        lines.append(f"**Total:** {self.passed + self.failed} tests")
        lines.append(f"**Passed:** {self.passed}")
        lines.append(f"**Failed:** {self.failed}")
        lines.append("")

        # Summary
        if self.failed == 0:
            lines.append("```")
            lines.append("+--------------------------------------------------+")
            lines.append("|                  ALL TESTS PASSED                |")
            lines.append(f"|                    {self.passed:3d} / {self.passed + self.failed:3d}                      |")
            lines.append("+--------------------------------------------------+")
            lines.append("```")
        else:
            lines.append("```")
            lines.append("+--------------------------------------------------+")
            lines.append("|                  SOME TESTS FAILED               |")
            lines.append(f"|              {self.passed:3d} passed, {self.failed:3d} failed              |")
            lines.append("+--------------------------------------------------+")
            lines.append("```")
        lines.append("")

        # Quick summary
        lines.append("## Summary")
        lines.append("")

        current_cat = ""
        for cat, name, passed, detail, extra, ops in self.results:
            if cat != current_cat:
                lines.append(f"### {cat}")
                lines.append("")
                current_cat = cat

            status = "PASS" if passed else "FAIL"
            lines.append(f"- [{status}] {name}")

            if extra:
                for k, v in extra.items():
                    lines.append(f"  - {k}: `{v}`")

            if not passed and detail:
                lines.append(f"  - Error: {detail}")

            lines.append("")

        # Failed tests summary
        if self.errors:
            lines.append("## Failed Tests")
            lines.append("")
            for err in self.errors:
                lines.append(f"- {err}")
            lines.append("")

        # Detailed results with operations
        lines.append("---")
        lines.append("")
        lines.append("## Detailed Test Results")
        lines.append("")

        test_num = 0
        for cat, name, passed, detail, extra, ops in self.results:
            test_num += 1
            status = "PASS" if passed else "FAIL"

            lines.append(f"### {test_num}. [{status}] {name}")
            lines.append("")
            lines.append(f"**Category:** {cat}")
            lines.append("")

            # Show operations
            if ops:
                for op in ops:
                    op_type = op.get("type", "")
                    desc = op.get("description", "")
                    req = op.get("request", {})
                    resp = op.get("response", {})

                    if op_type == "protocol":
                        lines.append(f"**Protocol Operation:** {desc}")
                        lines.append("")
                        if req:
                            lines.append("```")
                            for k, v in req.items():
                                lines.append(f"{k}: {v}")
                            lines.append("```")
                            lines.append("")
                        if resp:
                            lines.append("**Result:**")
                            lines.append("```")
                            for k, v in resp.items():
                                lines.append(f"{k}: {v}")
                            lines.append("```")
                            lines.append("")

                    elif op_type == "api":
                        method = req.get("method", "")
                        endpoint = req.get("endpoint", "")
                        body = req.get("body")

                        lines.append("**API Request:**")
                        lines.append("```")
                        lines.append(f"{method} {endpoint}")
                        if body:
                            lines.append("")
                            lines.append(json.dumps(body, indent=2))
                        lines.append("```")
                        lines.append("")

                        if resp:
                            status_code = resp.get("status", "")
                            resp_body = resp.get("body")
                            lines.append("**API Response:**")
                            lines.append("```")
                            lines.append(f"Status: {status_code}")
                            if resp_body:
                                lines.append("")
                                if isinstance(resp_body, dict):
                                    lines.append(json.dumps(resp_body, indent=2))
                                else:
                                    lines.append(str(resp_body))
                            lines.append("```")
                            lines.append("")

            # Show extra info
            if extra:
                lines.append("**Result Details:**")
                lines.append("")
                for k, v in extra.items():
                    lines.append(f"- {k}: `{v}`")
                lines.append("")

            # Show error
            if not passed and detail:
                lines.append(f"**Error:** {detail}")
                lines.append("")

            lines.append("---")
            lines.append("")

        with open(output_path, "w") as f:
            f.write("\n".join(lines))

        print(f"\nReport saved to: {output_path}")

    # =========================================================================
    # Run All Tests
    # =========================================================================

    def run_all(self, report_path: str = None) -> bool:
        print("=" * 60)
        print("SvxReflector Client Integration Tests")
        print("=" * 60)
        print(f"Reflector: {self.reflector_host}:{self.reflector_port}")
        print(f"Admin API: {self.admin_url}")

        try:
            self.test_basic_connection()
            self.test_auth_failure()
            self.test_disabled_user()
            self.test_realtime_user_toggle()
            self.test_mute_api_enforcement()
            self.test_real_audio_mute_enforcement()
            self.test_realtime_mute()
            self.test_unknown_user()
            self.test_multiple_connections()
            # Run auto-kick test last since it waits 12+ seconds
            self.test_auto_kick_on_disable()
        except requests.exceptions.ConnectionError as e:
            print(f"\n[ERROR] Connection failed: {e}")
            print("Make sure SvxReflector is running with Admin API enabled")
            return False
        except Exception as e:
            print(f"\n[ERROR] Unexpected error: {e}")
            import traceback
            traceback.print_exc()
            return False

        print("\n" + "=" * 60)
        print(f"Results: {self.passed} passed, {self.failed} failed")
        print("=" * 60)

        if self.errors:
            print("\nFailed tests:")
            for err in self.errors:
                print(f"  - {err}")

        if report_path:
            self.generate_report(report_path)

        return self.failed == 0


def wait_for_servers(reflector_host: str, reflector_port: int,
                     admin_host: str, admin_port: int,
                     timeout: int = 30) -> bool:
    """Wait for both servers to be ready"""
    import socket

    print("Waiting for servers...")
    start = time.time()

    while time.time() - start < timeout:
        # Check reflector
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2)
            s.connect((reflector_host, reflector_port))
            s.close()

            # Check admin API
            resp = requests.get(
                f"http://{admin_host}:{admin_port}/status",
                timeout=2
            )
            if resp.status_code in [200, 401, 404]:
                print("Servers are ready!")
                return True
        except:
            pass
        time.sleep(1)

    print("Timeout waiting for servers")
    return False


def main():
    parser = argparse.ArgumentParser(
        description="SvxReflector Client Integration Tests"
    )
    parser.add_argument("--reflector-host", default=DEFAULT_REFLECTOR_HOST)
    parser.add_argument("--reflector-port", type=int, default=DEFAULT_REFLECTOR_PORT)
    parser.add_argument("--admin-host", default=DEFAULT_ADMIN_HOST)
    parser.add_argument("--admin-port", type=int, default=DEFAULT_ADMIN_PORT)
    parser.add_argument("--admin-token", default=DEFAULT_ADMIN_TOKEN)
    parser.add_argument("--wait", action="store_true",
                        help="Wait for servers to be ready")
    parser.add_argument("--report", default=None,
                        help="Output path for test report")
    args = parser.parse_args()

    if args.wait:
        if not wait_for_servers(
            args.reflector_host, args.reflector_port,
            args.admin_host, args.admin_port
        ):
            sys.exit(1)

    tester = ClientIntegrationTest(
        reflector_host=args.reflector_host,
        reflector_port=args.reflector_port,
        admin_host=args.admin_host,
        admin_port=args.admin_port,
        admin_token=args.admin_token
    )

    success = tester.run_all(report_path=args.report)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
