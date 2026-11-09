import base64
import json
import socket
import threading
import time
import uuid


class JournalSystem:
    """Client for the application-side Journal execution/data service."""

    def __init__(self, request_timeout=None):
        self.client = None
        self._lock = threading.RLock()
        self._buffer = bytearray()
        self._next_request_id = 1
        self._address = None
        self._request_timeout = request_timeout

    def connect(self, host, port, timeout=3.0):
        self.disconnect()
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.settimeout(timeout)
        try:
            client.connect((host, int(port)))
        except Exception:
            client.close()
            raise
        client.settimeout(None)
        self.client = client
        self._address = (host, int(port))
        self._buffer.clear()

    def disconnect(self):
        if self.client is not None:
            try:
                self.client.close()
            finally:
                self.client = None
                self._buffer.clear()

    def _receive_frame(self, timeout=None):
        if self.client is None:
            raise ConnectionError("Journal client is not connected")
        previous_timeout = self.client.gettimeout()
        try:
            self.client.settimeout(timeout)
            while b":" not in self._buffer:
                chunk = self.client.recv(4096)
                if not chunk:
                    raise ConnectionError("Journal connection closed")
                self._buffer.extend(chunk)
            separator = self._buffer.index(ord(":"))
            header = bytes(self._buffer[:separator])
            if not header.isdigit():
                raise RuntimeError("Invalid Journal frame header")
            length = int(header)
            if length <= 0 or length > 128 * 1024 * 1024:
                raise RuntimeError("Invalid Journal frame length")
            del self._buffer[:separator + 1]
            while len(self._buffer) < length:
                chunk = self.client.recv(4096)
                if not chunk:
                    raise ConnectionError("Journal connection closed")
                self._buffer.extend(chunk)
            payload = bytes(self._buffer[:length])
            del self._buffer[:length]
            return payload
        finally:
            if self.client is not None:
                self.client.settimeout(previous_timeout)

    def request(self, method, params=None, timeout=None):
        if timeout is None:
            timeout = self._request_timeout
        if self.client is None:
            raise ConnectionError("Journal client is not connected")
        if not isinstance(method, str) or not method:
            raise ValueError("method must be a non-empty string")
        if params is not None and not isinstance(params, dict):
            raise TypeError("params must be a dict")
        with self._lock:
            request_id = self._next_request_id
            self._next_request_id += 1
            request = {
                "requestId": request_id,
                "method": method,
                "params": params or {},
            }
            payload = json.dumps(
                request, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.client.sendall(str(len(payload)).encode("ascii") + b":" + payload)
            response = json.loads(self._receive_frame(timeout).decode("utf-8"))
            if response.get("requestId") != request_id:
                raise RuntimeError(
                    f"Journal response id mismatch: expected {request_id}, "
                    f"got {response.get('requestId')}")
            if not response.get("success", False):
                raise RuntimeError(response.get("error", "Journal request failed"))
            result = response.get("result", {})
            if not isinstance(result, dict):
                raise RuntimeError("Journal response result must be an object")
            return result

    def version(self):
        return self.request("system.version")["version"]

    def paths(self):
        return self.request("journal.paths")

    def window_info(self, window):
        return self.request("app.window.info", {"window": window})

    def start_recording(self, record_mouse_moves=True, *, window_profile=None):
        params = {"recordMouseMoves": bool(record_mouse_moves)}
        if window_profile is not None:
            params["windowProfile"] = window_profile
        return self.request("journal.record.start", params)

    def recording_status(self):
        return self.request("journal.record.status", timeout=3.0)

    def stop_recording(self):
        return self.request("journal.record.stop")

    def begin_replay(self, timeout=30.0, *, window_profile=None):
        """Request playback; the application owns restart, never this client."""
        if self.client is None or self._address is None:
            raise ConnectionError("Journal client is not connected")
        if timeout <= 0:
            raise ValueError("Replay preparation timeout must be positive")
        replay_id = uuid.uuid4().hex
        deadline = time.monotonic() + timeout
        try:
            result = self.request(
                "journal.replay.begin", {"replayId": replay_id},
                timeout=min(3.0, timeout))
            self._check_replay_response(result, replay_id)
        except (OSError, ConnectionError):
            # An accepted request may lose its acknowledgement during shutdown.
            # Resume only: never submit another restart after uncertain delivery.
            pass
        return self._reconnect_for_replay(replay_id, deadline, window_profile)

    @staticmethod
    def _check_replay_response(result, replay_id):
        if (result.get("replayId") != replay_id or
                result.get("status") not in ("preparing", "ready")):
            raise RuntimeError("Invalid replay preparation response")

    def _reconnect_for_replay(self, replay_id, deadline, window_profile=None):
        host, port = self._address
        last_error = None
        while time.monotonic() < deadline:
            try:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self.connect(host, port, timeout=min(1.0, remaining))
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                params = {"replayId": replay_id, "resume": True}
                if window_profile is not None:
                    params["windowProfile"] = window_profile
                result = self.request(
                    "journal.replay.begin", params,
                    timeout=min(3.0, remaining))
                self._check_replay_response(result, replay_id)
                if result["status"] == "ready":
                    return result
            except (OSError, ConnectionError) as exc:
                last_error = exc
            except Exception:
                self.disconnect()
                raise
            self.disconnect()
            time.sleep(max(0.0, min(0.1, deadline - time.monotonic())))
        self.disconnect()
        raise TimeoutError("Application replay startup timed out") from last_error

    def end_replay(self):
        return self.request("journal.replay.end")

    def execute_entry(self, entry):
        if not isinstance(entry, dict):
            raise TypeError("entry must be a dict")
        return self.request("journal.entry.execute", {"entry": entry})

    @staticmethod
    def normalize_db_id(db_instance_id):
        if isinstance(db_instance_id, bool):
            raise TypeError("db_instance_id must be an integer or decimal string")
        value = str(db_instance_id).strip()
        digits = value[1:] if value.startswith("-") else value
        if not digits.isdigit() or int(value) == 0 or str(int(value)) != value:
            raise ValueError(f"Invalid DBInstanceID: {db_instance_id}")
        return value

    def serialize_db(self, db_instance_id):
        return self.request("app.db.serialize", {
            "dbInstanceId": self.normalize_db_id(db_instance_id),
        })

    def capture_screenshot(self):
        result = self.request("app.screenshot.capture")
        try:
            png = base64.b64decode(result.pop("pngBase64"), validate=True)
        except Exception as exc:
            raise RuntimeError("Application returned invalid PNG data") from exc
        result["png"] = png
        return result

    @staticmethod
    def entry(kind, payload, at_ms=0, sequence=0):
        if not isinstance(payload, dict):
            raise TypeError("payload must be a dict")
        return {
            "atMs": int(at_ms),
            "sequence": int(sequence),
            "kind": kind,
            "payload": payload,
        }

    def invoke_action(self, code, params=None):
        return self.execute_entry(self.entry(
            "action", {"code": code, "params": params or {}}))

    def send_mouse(self, event, x=None, y=None, *, global_x=None, global_y=None,
                   button=0, buttons=0, modifiers=0, window=None, target=None):
        if (x is None) != (y is None) or (x is None and target is None):
            raise ValueError("Supply both x/y, or a named target to use its center")
        payload = {
            "event": event,
            "button": button,
            "buttons": buttons,
            "modifiers": modifiers,
            "window": window or {},
        }
        if x is not None:
            payload["position"] = {"x": x, "y": y}
        if target is not None:
            payload["target"] = target
        else:
            payload["globalPosition"] = {
                "x": x if global_x is None else global_x,
                "y": y if global_y is None else global_y,
            }
        return self.execute_entry(self.entry("input.mouse", payload))

    def send_key(self, event, key, text="", modifiers=0, *, window=None,
                 auto_repeat=False, count=1, target=None):
        payload = {
            "event": event,
            "key": key,
            "text": text,
            "modifiers": modifiers,
            "autoRepeat": auto_repeat,
            "count": count,
            "window": window or {},
        }
        if target is not None:
            payload["target"] = target
        return self.execute_entry(self.entry("input.key", payload))

    def send_pointer(self, event, points, *, modifiers=0, window=None,
                     device_type=1, pointer_type=1):
        return self.execute_entry(self.entry("input.pointer", {
            "event": event,
            "points": points,
            "modifiers": modifiers,
            "deviceType": device_type,
            "pointerType": pointer_type,
            "window": window or {},
        }))

    def send_wheel(self, x, y, angle_delta, pixel_delta=(0, 0), *,
                   modifiers=0, buttons=0, window=None, target=None):
        payload = {
            "position": {"x": x, "y": y},
            "globalPosition": {"x": x, "y": y},
            "angleDelta": {"x": angle_delta[0], "y": angle_delta[1]},
            "pixelDelta": {"x": pixel_delta[0], "y": pixel_delta[1]},
            "modifiers": modifiers,
            "buttons": buttons,
            "phase": 0,
            "inverted": False,
            "window": window or {},
        }
        if target is not None:
            payload["target"] = target
            payload.pop("globalPosition")
        return self.execute_entry(self.entry("input.wheel", payload))

    def send_drag_drop(self, event, x=0, y=0, *, mime=None,
                       buttons=0, modifiers=0, possible_actions=0,
                       drop_action=0, window=None):
        encoded_mime = {}
        for name, value in (mime or {}).items():
            if isinstance(value, str):
                value = value.encode("utf-8")
            if not isinstance(value, bytes):
                raise TypeError("drag/drop MIME values must be bytes or strings")
            encoded_mime[name] = base64.b64encode(value).decode("ascii")
        return self.execute_entry(self.entry("input.dragDrop", {
            "event": event,
            "position": {"x": x, "y": y},
            "buttons": buttons,
            "modifiers": modifiers,
            "possibleActions": possible_actions,
            "dropAction": drop_action,
            "mime": encoded_mime,
            "window": window or {},
        }))

    def send_close(self, *, window=None):
        return self.execute_entry(self.entry(
            "input.close", {"window": window or {}}))
