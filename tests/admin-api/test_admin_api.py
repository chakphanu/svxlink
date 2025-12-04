#!/usr/bin/env python3
"""
Test suite for SvxReflector Admin API

Usage:
    python3 test_admin_api.py [--host HOST] [--port PORT] [--token TOKEN]
"""

import argparse
import json
import sys
import time
import os
from datetime import datetime
import requests
from typing import Optional, Dict, Any, List, Tuple

# Test configuration
DEFAULT_HOST = "localhost"
DEFAULT_PORT = 18080
DEFAULT_TOKEN = "test-token-12345"


class AdminAPITest:
    """Test class for Admin API endpoints"""

    def __init__(self, host: str, port: int, token: str):
        self.base_url = f"http://{host}:{port}"
        self.token = token
        self.headers = {
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json"
        }
        self.passed = 0
        self.failed = 0
        self.errors = []
        # (category, name, passed, detail, req_info, resp_info)
        self.results: List[Tuple[str, str, bool, str, Dict, Dict]] = []
        self.current_category = ""
        self.last_request: Dict[str, Any] = {}
        self.last_response: Dict[str, Any] = {}

    def log(self, msg: str):
        print(f"  {msg}")

    def set_category(self, category: str):
        self.current_category = category

    def record_request(self, method: str, endpoint: str, body: Any = None):
        """Record request info for report"""
        self.last_request = {
            "method": method,
            "endpoint": endpoint,
            "body": body
        }

    def record_response(self, resp: requests.Response):
        """Record response info for report"""
        try:
            body = resp.json()
        except:
            body = resp.text[:200] if resp.text else None
        self.last_response = {
            "status": resp.status_code,
            "body": body
        }

    def test(self, name: str, condition: bool, detail: str = ""):
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
            self.last_request.copy(),
            self.last_response.copy()
        ))

    def get(self, endpoint: str, auth: bool = True) -> requests.Response:
        headers = self.headers if auth else {}
        self.record_request("GET", endpoint, None)
        resp = requests.get(f"{self.base_url}{endpoint}", headers=headers, timeout=5)
        self.record_response(resp)
        return resp

    def post(self, endpoint: str, data: Dict[str, Any], auth: bool = True) -> requests.Response:
        headers = self.headers if auth else {"Content-Type": "application/json"}
        self.record_request("POST", endpoint, data)
        resp = requests.post(
            f"{self.base_url}{endpoint}",
            headers=headers,
            json=data,
            timeout=5
        )
        self.record_response(resp)
        return resp

    def put(self, endpoint: str, data: Dict[str, Any], auth: bool = True) -> requests.Response:
        headers = self.headers if auth else {"Content-Type": "application/json"}
        self.record_request("PUT", endpoint, data)
        resp = requests.put(
            f"{self.base_url}{endpoint}",
            headers=headers,
            json=data,
            timeout=5
        )
        self.record_response(resp)
        return resp

    def delete(self, endpoint: str, auth: bool = True) -> requests.Response:
        headers = self.headers if auth else {}
        self.record_request("DELETE", endpoint, None)
        resp = requests.delete(f"{self.base_url}{endpoint}", headers=headers, timeout=5)
        self.record_response(resp)
        return resp

    # =========================================================================
    # Test: Authentication
    # =========================================================================
    def test_auth(self):
        print("\n[TEST] Authentication")
        self.set_category("Authentication")

        # Test without token
        resp = self.get("/admin/status", auth=False)
        self.test("No token returns 401", resp.status_code == 401)

        # Test with wrong token
        headers = {"Authorization": "Bearer wrong-token"}
        self.record_request("GET", "/admin/status (wrong token)", None)
        resp = requests.get(f"{self.base_url}/admin/status", headers=headers, timeout=5)
        self.record_response(resp)
        self.test("Wrong token returns 401", resp.status_code == 401)

        # Test with correct token
        resp = self.get("/admin/status")
        self.test("Correct token returns 200", resp.status_code == 200)

    # =========================================================================
    # Test: Status endpoint
    # =========================================================================
    def test_status(self):
        print("\n[TEST] GET /admin/status")
        self.set_category("GET /admin/status")

        resp = self.get("/admin/status")
        self.test("Returns 200", resp.status_code == 200)

        data = resp.json()
        self.test("Has 'connected_nodes' field", "connected_nodes" in data)
        self.test("Has 'muted_nodes' field", "muted_nodes" in data)
        self.test("Has 'admin_api_version' field", "admin_api_version" in data)

    # =========================================================================
    # Test: Mute endpoints
    # =========================================================================
    def test_mute(self):
        print("\n[TEST] POST /admin/mute (temporary)")
        self.set_category("POST /admin/mute")

        # Mute a test callsign for 60 seconds
        resp = self.post("/admin/mute", {
            "callsign": "TEST1",
            "duration": 60,
            "reason": "Test mute"
        })
        self.test("Mute returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        if resp.status_code == 200:
            data = resp.json()
            self.test("Response has success=true", data.get("success") == True)

        # Try to mute again (should fail - already muted)
        resp = self.post("/admin/mute", {
            "callsign": "TEST1",
            "duration": 60
        })
        self.test("Duplicate mute returns 409", resp.status_code == 409)

    def test_mute_forever(self):
        print("\n[TEST] POST /admin/mute (forever)")
        self.set_category("POST /admin/mute (forever)")

        resp = self.post("/admin/mute", {
            "callsign": "TEST2",
            "reason": "Permanent test mute"
        })
        self.test("Mute forever returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

    def test_mute_list(self):
        print("\n[TEST] GET /admin/mute (list)")
        self.set_category("GET /admin/mute")

        resp = self.get("/admin/mute")
        self.test("List returns 200", resp.status_code == 200)

        if resp.status_code == 200:
            data = resp.json()
            self.test("Has 'count' field", "count" in data)
            self.test("Has 'muted_nodes' array", "muted_nodes" in data)
            self.test("Count >= 2 (TEST1, TEST2)", data.get("count", 0) >= 2,
                      f"Got count={data.get('count')}")

            # Check that our test callsigns are in the list
            callsigns = [n["callsign"] for n in data.get("muted_nodes", [])]
            self.test("TEST1 in mute list", "TEST1" in callsigns)
            self.test("TEST2 in mute list", "TEST2" in callsigns)

    def test_mute_validation(self):
        print("\n[TEST] POST /admin/mute (validation)")
        self.set_category("Mute Validation")

        # Missing callsign
        resp = self.post("/admin/mute", {"reason": "test"})
        self.test("Missing callsign returns 400", resp.status_code == 400)

        # Empty body
        self.record_request("POST", "/admin/mute (empty body)", None)
        resp = requests.post(
            f"{self.base_url}/admin/mute",
            headers=self.headers,
            data="",
            timeout=5
        )
        self.record_response(resp)
        self.test("Empty body returns 400", resp.status_code == 400)

    # =========================================================================
    # Test: Unmute endpoint
    # =========================================================================
    def test_unmute(self):
        print("\n[TEST] POST /admin/unmute")
        self.set_category("POST /admin/unmute")

        # Unmute TEST1
        resp = self.post("/admin/unmute", {"callsign": "TEST1"})
        self.test("Unmute returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        # Try to unmute again (should fail - not in list)
        resp = self.post("/admin/unmute", {"callsign": "TEST1"})
        self.test("Unmute non-existent returns 404", resp.status_code == 404)

        # Unmute TEST2
        resp = self.post("/admin/unmute", {"callsign": "TEST2"})
        self.test("Unmute TEST2 returns 200", resp.status_code == 200)

        # Verify list is empty
        resp = self.get("/admin/mute")
        if resp.status_code == 200:
            data = resp.json()
            self.test("Mute list is empty", data.get("count", -1) == 0,
                      f"Got count={data.get('count')}")

    # =========================================================================
    # Test: Kick endpoint
    # =========================================================================
    def test_kick(self):
        print("\n[TEST] POST /admin/kick")
        self.set_category("POST /admin/kick")

        # Try to kick a non-existent node
        resp = self.post("/admin/kick", {
            "callsign": "NONEXISTENT",
            "reason": "Test kick"
        })
        self.test("Kick non-existent returns 404", resp.status_code == 404)

        # Missing callsign
        resp = self.post("/admin/kick", {"reason": "test"})
        self.test("Missing callsign returns 400", resp.status_code == 400)

    # =========================================================================
    # Test: Config endpoints (stub - should return 501)
    # =========================================================================
    def test_config(self):
        print("\n[TEST] /admin/config (stub)")
        self.set_category("/admin/config")

        resp = self.get("/admin/config")
        self.test("GET config returns 501", resp.status_code == 501)

        resp = self.post("/admin/config", {"key": "test", "value": "test"})
        self.test("POST config returns 501", resp.status_code == 501)

    # =========================================================================
    # Test: Method not allowed
    # =========================================================================
    def test_method_not_allowed(self):
        print("\n[TEST] Method Not Allowed")
        self.set_category("Method Not Allowed")

        # DELETE on /admin/mute
        self.record_request("DELETE", "/admin/mute", None)
        resp = requests.delete(
            f"{self.base_url}/admin/mute",
            headers=self.headers,
            timeout=5
        )
        self.record_response(resp)
        self.test("DELETE /admin/mute returns 405", resp.status_code == 405)

        # POST on /admin/status
        resp = self.post("/admin/status", {})
        self.test("POST /admin/status returns 405", resp.status_code == 405)

    # =========================================================================
    # Test: Not found
    # =========================================================================
    def test_not_found(self):
        print("\n[TEST] Not Found")
        self.set_category("Not Found")

        resp = self.get("/admin/nonexistent")
        self.test("Unknown endpoint returns 404", resp.status_code == 404)

    # =========================================================================
    # Test: User CRUD endpoints
    # =========================================================================
    def test_users_list(self):
        print("\n[TEST] GET /admin/users (list)")
        self.set_category("GET /admin/users")

        resp = self.get("/admin/users")
        self.test("List users returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        if resp.status_code == 200:
            data = resp.json()
            self.test("Has 'count' field", "count" in data)
            self.test("Has 'users' array", "users" in data)

    def test_users_create(self):
        print("\n[TEST] POST /admin/users (create)")
        self.set_category("POST /admin/users")

        # Create a test user
        resp = self.post("/admin/users", {
            "callsign": "TEST-USER",
            "group": "test-group",
            "password": "test-password",
            "metadata": {"email": "test@example.com"}
        })
        self.test("Create user returns 201", resp.status_code == 201,
                  f"Got {resp.status_code}: {resp.text}")

        if resp.status_code == 201:
            data = resp.json()
            self.test("Response has success=true", data.get("success") == True)
            self.test("Response has user object", "user" in data)
            if "user" in data:
                self.test("User callsign is correct",
                          data["user"].get("callsign") == "TEST-USER")
                self.test("User source is 'database'",
                          data["user"].get("source") == "database")

        # Try to create duplicate user
        resp = self.post("/admin/users", {
            "callsign": "TEST-USER",
            "group": "test-group",
            "password": "test-password"
        })
        self.test("Duplicate user returns 409", resp.status_code == 409)

        # Missing required fields
        resp = self.post("/admin/users", {"callsign": "TEST2"})
        self.test("Missing group returns 400", resp.status_code == 400)

        resp = self.post("/admin/users", {"callsign": "TEST2", "group": "g"})
        self.test("Missing password returns 400", resp.status_code == 400)

    def test_users_get(self):
        print("\n[TEST] GET /admin/users/{callsign}")
        self.set_category("GET /admin/users/{id}")

        # Get existing user
        resp = self.get("/admin/users/TEST-USER")
        self.test("Get user returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        if resp.status_code == 200:
            data = resp.json()
            self.test("Has callsign field", data.get("callsign") == "TEST-USER")
            self.test("Has enabled field", "enabled" in data)
            self.test("Has metadata field", "metadata" in data)

        # Get non-existent user
        resp = self.get("/admin/users/NONEXISTENT")
        self.test("Get non-existent user returns 404", resp.status_code == 404)

    def test_users_update(self):
        print("\n[TEST] PUT /admin/users/{callsign}")
        self.set_category("PUT /admin/users/{id}")

        # Update user
        resp = self.put("/admin/users/TEST-USER", {
            "enabled": False,
            "metadata": {"email": "updated@example.com"}
        })
        self.test("Update user returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        if resp.status_code == 200:
            data = resp.json()
            self.test("Response has success=true", data.get("success") == True)
            if "user" in data:
                self.test("User is now disabled",
                          data["user"].get("enabled") == False)

        # Update non-existent user
        resp = self.put("/admin/users/NONEXISTENT", {"enabled": False})
        self.test("Update non-existent user returns 404", resp.status_code == 404)

    def test_users_delete(self):
        print("\n[TEST] DELETE /admin/users/{callsign}")
        self.set_category("DELETE /admin/users/{id}")

        # Delete user
        resp = self.delete("/admin/users/TEST-USER")
        self.test("Delete user returns 200", resp.status_code == 200,
                  f"Got {resp.status_code}: {resp.text}")

        # Verify user is gone
        resp = self.get("/admin/users/TEST-USER")
        self.test("Deleted user not found", resp.status_code == 404)

        # Delete non-existent user
        resp = self.delete("/admin/users/NONEXISTENT")
        self.test("Delete non-existent user returns 404", resp.status_code == 404)

    # =========================================================================
    # Generate TEST-RESULT.md
    # =========================================================================
    def generate_report(self, output_path: str):
        """Generate a markdown test report with ASCII table and request/response details"""
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Group results by category
        categories = {}
        for cat, name, passed, detail, req_info, resp_info in self.results:
            if cat not in categories:
                categories[cat] = []
            categories[cat].append((name, passed, detail, req_info, resp_info))

        # Calculate column widths
        max_cat_len = max(len(cat) for cat in categories.keys()) if categories else 20
        max_test_len = max(len(name) for _, name, _, _, _, _ in self.results) if self.results else 30
        max_cat_len = max(max_cat_len, 20)
        max_test_len = max(max_test_len, 30)

        lines = []
        lines.append("# SvxReflector Admin API Test Results")
        lines.append("")
        lines.append(f"**Date:** {now}")
        lines.append(f"**Target:** {self.base_url}")
        lines.append(f"**Total:** {self.passed + self.failed} tests")
        lines.append(f"**Passed:** {self.passed}")
        lines.append(f"**Failed:** {self.failed}")
        lines.append("")

        # Summary box
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

        # Detailed results table
        lines.append("## Summary Table")
        lines.append("")

        # Table header
        lines.append("```")
        sep = f"+{'-' * (max_cat_len + 2)}+{'-' * (max_test_len + 2)}+--------+"
        lines.append(sep)
        lines.append(f"| {'Category'.ljust(max_cat_len)} | {'Test'.ljust(max_test_len)} | Status |")
        lines.append(sep)

        # Table rows grouped by category
        for cat in categories.keys():
            first = True
            for name, passed, detail, req_info, resp_info in categories[cat]:
                status = " PASS " if passed else " FAIL "
                cat_display = cat if first else ""
                lines.append(f"| {cat_display.ljust(max_cat_len)} | {name.ljust(max_test_len)} |{status}|")
                first = False
            lines.append(sep)

        lines.append("```")
        lines.append("")

        # Failed tests section
        if self.errors:
            lines.append("## Failed Tests")
            lines.append("")
            for err in self.errors:
                lines.append(f"- {err}")
            lines.append("")

        # Detailed request/response section
        lines.append("## Detailed Request/Response")
        lines.append("")

        test_num = 1
        for cat, name, passed, detail, req_info, resp_info in self.results:
            status_icon = "PASS" if passed else "FAIL"
            lines.append(f"### {test_num}. [{status_icon}] {name}")
            lines.append("")
            lines.append(f"**Category:** {cat}")
            lines.append("")

            # Request details
            if req_info:
                method = req_info.get("method", "?")
                endpoint = req_info.get("endpoint", "?")
                req_body = req_info.get("body")

                lines.append("**Request:**")
                lines.append("```")
                lines.append(f"{method} {endpoint}")
                if req_body is not None:
                    lines.append("")
                    lines.append(json.dumps(req_body, indent=2))
                lines.append("```")
                lines.append("")

            # Response details
            if resp_info:
                status_code = resp_info.get("status", "?")
                resp_body = resp_info.get("body")

                lines.append("**Response:**")
                lines.append("```")
                lines.append(f"Status: {status_code}")
                if resp_body is not None:
                    lines.append("")
                    if isinstance(resp_body, (dict, list)):
                        lines.append(json.dumps(resp_body, indent=2))
                    else:
                        lines.append(str(resp_body))
                lines.append("```")
                lines.append("")

            # Show detail if test failed
            if not passed and detail:
                lines.append(f"**Error:** {detail}")
                lines.append("")

            lines.append("---")
            lines.append("")
            test_num += 1

        # Write to file
        with open(output_path, "w") as f:
            f.write("\n".join(lines))

        print(f"\nReport saved to: {output_path}")

    # =========================================================================
    # Run all tests
    # =========================================================================
    def run_all(self, report_path: Optional[str] = None) -> bool:
        print("=" * 60)
        print("SvxReflector Admin API Test Suite")
        print("=" * 60)
        print(f"Target: {self.base_url}")
        print(f"Token: {self.token[:10]}...")

        try:
            self.test_auth()
            self.test_status()
            self.test_mute()
            self.test_mute_forever()
            self.test_mute_list()
            self.test_mute_validation()
            self.test_unmute()
            self.test_kick()
            self.test_config()
            self.test_method_not_allowed()
            self.test_not_found()
            self.test_users_list()
            self.test_users_create()
            self.test_users_get()
            self.test_users_update()
            self.test_users_delete()
        except requests.exceptions.ConnectionError as e:
            print(f"\n[ERROR] Connection failed: {e}")
            return False
        except Exception as e:
            print(f"\n[ERROR] Unexpected error: {e}")
            return False

        print("\n" + "=" * 60)
        print(f"Results: {self.passed} passed, {self.failed} failed")
        print("=" * 60)

        if self.errors:
            print("\nFailed tests:")
            for err in self.errors:
                print(f"  - {err}")

        # Generate report if path specified
        if report_path:
            self.generate_report(report_path)

        return self.failed == 0


def wait_for_server(host: str, port: int, timeout: int = 30) -> bool:
    """Wait for the server to be ready"""
    print(f"Waiting for server at {host}:{port}...")
    start = time.time()
    while time.time() - start < timeout:
        try:
            resp = requests.get(f"http://{host}:{port}/status", timeout=2)
            if resp.status_code in [200, 401, 404]:
                print("Server is ready!")
                return True
        except requests.exceptions.ConnectionError:
            pass
        time.sleep(1)
    print("Timeout waiting for server")
    return False


def main():
    parser = argparse.ArgumentParser(description="Test SvxReflector Admin API")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Server host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Server port")
    parser.add_argument("--token", default=DEFAULT_TOKEN, help="Auth token")
    parser.add_argument("--wait", action="store_true", help="Wait for server to be ready")
    parser.add_argument("--report", default=None, help="Output path for TEST-RESULT.md")
    args = parser.parse_args()

    if args.wait:
        if not wait_for_server(args.host, args.port):
            sys.exit(1)

    tester = AdminAPITest(args.host, args.port, args.token)
    success = tester.run_all(report_path=args.report)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
