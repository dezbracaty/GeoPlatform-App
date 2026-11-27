"""Prepare Journal runtime paths and apply independent checks to native results."""

import base64
import hashlib
import json
from pathlib import Path


def screenshot_action_path(entry, output, index):
    if entry["kind"] == "action" and entry["payload"].get("code") == "screenshot.capture":
        filepath = entry["payload"].get("params", {}).get("filepath")
        return (output / (filepath or f"screenshots/screenshot_{index:03d}_actual.png")).resolve()
    return None


def runtime_document(source, output):
    document = json.loads(source)
    index = 0
    for entry in document["entries"]:
        if isinstance(entry, str):
            continue
        index += 1
        screenshot = screenshot_action_path(entry, output, index)
        if screenshot is not None:
            screenshot.parent.mkdir(parents=True, exist_ok=True)
            entry["payload"].setdefault("params", {})["filepath"] = str(screenshot)
            entry["payload"].pop("baseline", None)
    return json.dumps(document, ensure_ascii=False, separators=(",", ":"))


class ReplayEvidence:
    def __init__(self, journal_path, output, validate, comparisons, compare_db):
        self.journal_path = journal_path
        self.output = output
        self.validate = validate
        self.comparisons = comparisons
        self.compare_db = compare_db
        self.screenshots = {}

    def prepare(self, entries):
        if not self.validate:
            return
        for index, entry in enumerate(entries, start=1):
            actual = screenshot_action_path(entry, self.output, index)
            if entry["kind"] == "verify.screenshot":
                actual = self.output / "screenshots" / f"screenshot_{index:03d}_actual.png"
            if actual is not None:
                try:
                    self.screenshots[index] = self.comparisons.prepare(
                        self.journal_path, self.output, entry["payload"], index, actual)
                except Exception as exc:
                    self.screenshots[index] = exc

    def process(self, entry, native):
        result = {key: native[key] for key in ("index", "kind", "atMs", "status")}
        result.update(message=native.get("message", "executed"), evidence=[])
        if "uiReadiness" in native:
            result["uiReadiness"] = native["uiReadiness"]
        if entry["kind"] == "action":
            result["actionCode"] = entry["payload"]["code"]
        if result["status"] == "skipped":
            result["message"] = "validation disabled by caller"
        if result["status"] != "passed":
            return result

        index = result["index"]
        try:
            screenshot = screenshot_action_path(entry, self.output, index)
            if screenshot is not None:
                if not screenshot.is_file() or screenshot.stat().st_size == 0:
                    raise RuntimeError(f"Screenshot Action did not save an image: {screenshot}")
                result["evidence"].append(screenshot)
                if self.validate:
                    self.compare_screenshot(result, screenshot)
                else:
                    result.update(message="Candidate screenshot saved; not verification",
                                  comparison=dict(status="skipped"))
            elif entry["kind"] == "verify.screenshot":
                screenshot = self.output / "screenshots" / f"screenshot_{index:03d}_actual.png"
                prepared = self.screenshots[index]
                if isinstance(prepared, Exception):
                    raise prepared
                screenshot.write_bytes(base64.b64decode(native["data"]["pngBase64"], validate=True))
                result["evidence"].append(screenshot)
                self.compare_screenshot(result, screenshot)
            elif entry["kind"] in {"capture.db", "verify.db"}:
                self.process_db(entry, native["data"], result)
        except Exception as exc:
            result.update(status="failed", message=str(exc))
        return result

    def compare_screenshot(self, result, actual):
        prepared = self.screenshots[result["index"]]
        if isinstance(prepared, Exception):
            raise prepared
        # Native pauses while this independent check owns the captured bytes.
        # A failed comparison never releases the next native operation.
        result.update(self.comparisons.compare_checkpoint(prepared, actual))

    def process_db(self, entry, actual, result):
        index = result["index"]
        stem = self.output / "reports" / f"db_{index:03d}"
        actual_path = stem.with_name(f"{stem.name}_actual.bin")
        if entry["kind"] == "capture.db":
            raw = base64.b64decode(actual["serialized"], validate=True)
            actual_path.write_bytes(raw)
            metadata_path = actual_path.with_suffix(".json")
            metadata_path.write_text(json.dumps({
                "dbInstanceId": actual["dbInstanceId"],
                "serializationFormat": actual["serializationFormat"],
                "byteCount": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            result.update(evidence=[actual_path, metadata_path], message="raw DB data captured; no baseline comparison")
            return

        payload = entry["payload"]
        expected_format, actual_format = payload["serializationFormat"], actual["serializationFormat"]
        if expected_format != actual_format:
            raise RuntimeError(f"DB serialization format mismatch: {expected_format!r} != {actual_format!r}")
        comparison, expected, raw = self.compare_db(payload["expectedSerialized"], actual["serialized"])
        expected_path = stem.with_name(f"{stem.name}_expected.bin")
        comparison_path = stem.with_name(f"{stem.name}_comparison.json")
        expected_path.write_bytes(expected)
        actual_path.write_bytes(raw)
        comparison_path.write_text(json.dumps(comparison, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        result["evidence"] = [expected_path, actual_path, comparison_path]
        if comparison["equal"]:
            result["message"] = "complete TransDB state matches"
        else:
            result.update(status="failed", message=(
                "DB serialized data mismatch: first byte difference at "
                f"offset {comparison['firstDifferenceOffset']}"))
