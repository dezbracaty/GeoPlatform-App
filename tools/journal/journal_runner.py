import base64
import argparse
import copy
import hashlib
import html
import json
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path
from urllib.parse import quote
from session_repository import JournalSessionRepository
from journal_display_profile import WINDOW_PROFILE, wait_for_window


FORMAT_VERSION = 2
DB_SERIALIZATION_FORMAT = "transdb.cereal-portable-binary.v1"
# Only these reviewed native-import question images contain the continuously
# animated task indicator. Other stages, including cancel/retry, remain exact.
NATIVE_IMPORT_PROGRESS_BASELINES = {
    ("three_mf_filepicker_geometry", "import-mode.png"),
    ("three_mf_native_decision_cancel", "native-question.png"),
    ("three_mf_target2_geometry", "native-options.png"),
}


def _native_import_progress_baseline(path):
    parts = Path(path).parts
    return (sys.platform == "darwin" and len(parts) >= 4
            and parts[-4:-2] == ("baselines", "screenshots")
            and parts[-2:] in NATIVE_IMPORT_PROGRESS_BASELINES)


ENTRY_KINDS = {
    "input.mouse",
    "input.key",
    "input.pointer",
    "input.wheel",
    "input.dragDrop",
    "input.close",
    "action",
    "verify.db",
    "verify.screenshot",
    "capture.db",
}


class JournalFormatError(ValueError):
    pass


def _screenshot_region(value):
    """Optional physical-pixel [x, y, width, height] comparison rectangle."""
    if value is None:
        return None
    if (not isinstance(value, (list, tuple)) or len(value) != 4 or
            any(type(number) is not int for number in value) or
            value[0] < 0 or value[1] < 0 or value[2] <= 0 or value[3] <= 0):
        raise ValueError("screenshot region must be [x, y, width, height] with nonnegative origin and positive size")
    return tuple(value)



def _screenshot_regions(region=None, regions=None):
    if regions is None:
        single = _screenshot_region(region)
        return None if single is None else [single]
    if region is not None:
        raise ValueError("screenshot region and regions are mutually exclusive")
    if not isinstance(regions, list) or not regions:
        raise ValueError("screenshot regions must be a non-empty array of rectangles")
    rectangles = [_screenshot_region(value) for value in regions]
    if any(value is None for value in rectangles):
        raise ValueError("screenshot regions cannot contain null")
    return rectangles


def _max_channel_difference(value):
    if type(value) is not int or not 0 <= value <= 255:
        raise ValueError("screenshot maxChannelDifference must be an integer from 0 to 255")
    return value


def _blur_radius(value):
    if type(value) is not int or not 0 <= value <= 2:
        raise ValueError("screenshot blurRadius must be an integer from 0 to 2 physical pixels")
    return value


def load_journal(path):
    path = Path(path)
    source = path.read_bytes()
    document = json.loads(source)
    if not isinstance(document, dict):
        raise JournalFormatError("Journal root must be an object")
    if document.get("formatVersion") != FORMAT_VERSION:
        raise JournalFormatError("Only Journal formatVersion 2 is supported")
    if set(document) != {"formatVersion", "entries"}:
        raise JournalFormatError("Journal fields must be formatVersion/entries; descriptions belong in metadata.json")
    entries = document.get("entries")
    if not isinstance(entries, list) or not entries:
        raise JournalFormatError("Journal v2 requires a non-empty entries array")

    previous_time = -1
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise JournalFormatError(f"entries[{index}] must be an object")
        if set(entry) != {"atMs", "kind", "payload"}:
            raise JournalFormatError(f"entries[{index}] fields must be atMs/kind/payload; array order defines sequence")
        if (isinstance(entry["atMs"], bool) or
                not isinstance(entry["atMs"], int) or entry["atMs"] < 0):
            raise JournalFormatError(f"entries[{index}].atMs is invalid")
        if entry["atMs"] < previous_time:
            raise JournalFormatError("Journal entries must be ordered by atMs")
        previous_time = entry["atMs"]
        # The wire entry still requires sequence; the file has only array order.
        entry["sequence"] = index + 1
        if entry["kind"] not in ENTRY_KINDS:
            raise JournalFormatError(
                f"entries[{index}] has unsupported kind {entry['kind']!r}")
        if not isinstance(entry["payload"], dict):
            raise JournalFormatError(f"entries[{index}].payload must be an object")
        payload = entry["payload"]
        if entry["kind"] == "action" and not payload.get("code"):
            raise JournalFormatError(f"entries[{index}] action has no code")
        if entry["kind"] == "capture.db":
            from journalsystem import JournalSystem
            try:
                JournalSystem.normalize_db_id(payload.get("dbInstanceId"))
            except (TypeError, ValueError) as exc:
                raise JournalFormatError(f"entries[{index}] invalid DB capture ID") from exc
        if entry["kind"] == "verify.db":
            db_instance_id = payload.get("dbInstanceId")
            if (not isinstance(db_instance_id, str) or
                    not db_instance_id or
                    (db_instance_id.startswith("-") and
                     not db_instance_id[1:].isdigit()) or
                    (not db_instance_id.startswith("-") and
                     not db_instance_id.isdigit()) or
                    int(db_instance_id) == 0 or
                    str(int(db_instance_id)) != db_instance_id or
                    payload.get("serializationFormat") !=
                    DB_SERIALIZATION_FORMAT or
                    not isinstance(payload.get("expectedSerialized"), str)):
                raise JournalFormatError(
                    f"entries[{index}] DB checkpoint is incomplete")
            try:
                base64.b64decode(
                    payload["expectedSerialized"].encode("ascii"), validate=True)
            except (UnicodeEncodeError, ValueError) as exc:
                raise JournalFormatError(
                    f"entries[{index}] DB checkpoint has invalid serialized data") from exc
        if entry["kind"] == "verify.screenshot":
            tolerance = payload.get("tolerance", 0.99)
            if not isinstance(payload.get("baseline"), str) or not payload["baseline"]:
                raise JournalFormatError(
                    f"entries[{index}] screenshot checkpoint has no baseline")
            if (isinstance(tolerance, bool) or
                    not isinstance(tolerance, (int, float)) or
                    not 0.0 <= tolerance <= 1.0):
                raise JournalFormatError(
                    f"entries[{index}] screenshot tolerance is invalid")
            try:
                _screenshot_regions(payload.get("region"), payload.get("regions"))
                _max_channel_difference(payload.get("maxChannelDifference", 0))
                _blur_radius(payload.get("blurRadius", 0))
            except ValueError as exc:
                raise JournalFormatError(f"entries[{index}] {exc}") from exc
    document["journalPath"] = str(path.resolve())
    document["journalSha256"] = hashlib.sha256(source).hexdigest()
    return document


def write_journal(path, entries):
    path = Path(path)
    if not entries:
        raise JournalFormatError("Cannot write an empty Journal v2")
    ordered = sorted(entries, key=lambda item: (item["atMs"], item["sequence"]))
    document = {
        "formatVersion": FORMAT_VERSION,
        "entries": [{key: entry[key] for key in ("atMs", "kind", "payload")}
                    for entry in ordered],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    os.replace(temporary, path)
    return path


def compare_serialized_data(expected_base64, actual_base64):
    """Compare two opaque DB serializations without inspecting DB fields."""
    try:
        expected = base64.b64decode(expected_base64.encode("ascii"), validate=True)
        actual = base64.b64decode(actual_base64.encode("ascii"), validate=True)
    except (AttributeError, UnicodeEncodeError, ValueError) as exc:
        raise ValueError("DB serialized data is not valid Base64") from exc

    first_difference = next(
        (index for index, pair in enumerate(zip(expected, actual))
         if pair[0] != pair[1]),
        None)
    if first_difference is None and len(expected) != len(actual):
        first_difference = min(len(expected), len(actual))

    details = {
        "equal": expected == actual,
        "expectedSize": len(expected),
        "actualSize": len(actual),
        "expectedSha256": hashlib.sha256(expected).hexdigest(),
        "actualSha256": hashlib.sha256(actual).hexdigest(),
        "firstDifferenceOffset": first_difference,
    }
    return details, expected, actual


def compare_screenshots(expected_path, actual_png, tolerance, diff_path, region=None,
                        *, regions=None, max_channel_difference=0, blur_radius=0, details=None,
                        exclude_fps=False, exclude_import_progress=False):
    rectangles = _screenshot_regions(region, regions)
    channel_limit = _max_channel_difference(max_channel_difference)
    radius = _blur_radius(blur_radius)
    try:
        from PIL import Image, ImageChops, ImageDraw, ImageFilter
    except ImportError as exc:
        raise RuntimeError(
            "Screenshot validation requires Pillow: python3 -m pip install Pillow") from exc

    from io import BytesIO
    with Image.open(expected_path) as source:
        expected = source.convert("RGBA")
    with Image.open(BytesIO(actual_png)) as source:
        actual = source.convert("RGBA")
    if details is not None:
        details.update(expectedSize=list(expected.size), actualSize=list(actual.size), differentPixels=None)
    if expected.size != actual.size:
        actual.save(diff_path)
        return False, 0.0, f"size mismatch: {expected.size} != {actual.size}"

    # Compare the union in one captured frame. Overlaps count once; pixels
    # outside the declared regions never dilute the matching fraction.
    selected = Image.new("L", expected.size, 0)
    painter = ImageDraw.Draw(selected)
    for x, y, width, height in rectangles or [(0, 0, *expected.size)]:
        if x + width > expected.width or y + height > expected.height:
            raise ValueError(f"screenshot region {[x, y, width, height]} exceeds image size {expected.size}")
        painter.rectangle((x, y, x + width - 1, y + height - 1), fill=255)
    excluded = []
    if exclude_fps or exclude_import_progress:
        # The compact FPS button sits at the top right of the shared 1382x954
        # window. Include its changing width and shadow, scaled to native DPR.
        # Other window sizes/cropped images get no implicit exclusion.
        logical_width, logical_height = WINDOW_PROFILE['logicalSize']
        scale = expected.width / logical_width
        if round(logical_height * scale) == expected.height:
            # The Windows title/command area is six logical pixels taller.
            # Keep the same compact-button footprint on each native layout.
            fps_top = 78 if sys.platform == 'win32' else 72
            areas = [('fps', (1260, fps_top, 1370, fps_top + 36))] if exclude_fps else []
            if exclude_import_progress and expected.size == (1382, 954):
                # GlobalPrintCommandBar.taskContent: rotating 16px icon and
                # the 300x5 indeterminate content, excluding its status label.
                areas += [('native-import-spinner', (736, 894, 760, 918)),
                          ('native-import-progress', (766, 913, 1066, 918))]
            for reason, bounds in areas:
                left, top, right, bottom = [round(value * scale) for value in bounds]
                painter.rectangle((left, top, right - 1, bottom - 1), fill=0)
                excluded.append(dict(reason=reason, rectangle=[left, top, right - left, bottom - top]))
    total = selected.histogram()[255]
    if not total:
        raise ValueError('Screenshot comparison has no pixels outside excluded areas')
    if details is not None:
        details.update(excludedRegions=excluded, comparedPixels=total)

    # Filter only the comparison inputs, never the saved expected/actual evidence.
    # The explicit physical-pixel radius bounds the spatial loss of detail.
    reference = expected
    if radius:
        expected = expected.filter(ImageFilter.GaussianBlur(radius))
        actual = actual.filter(ImageFilter.GaussianBlur(radius))
    channels = ImageChops.difference(expected, actual).split()
    changed = channels[0]
    for channel in channels[1:]:
        changed = ImageChops.lighter(changed, channel)
    changed_mask = ImageChops.multiply(
        changed.point(lambda value: 255 if value > channel_limit else 0), selected)
    different = changed_mask.histogram()[255]
    similarity = (total - different) / total
    if details is not None:
        details['differentPixels'] = different
    Image.composite(
        Image.new("RGBA", expected.size, (255, 0, 0, 255)),
        reference, changed_mask).save(diff_path)
    reason = "pixel similarity"
    if excluded:
        reason += " excluding " + ", ".join(f"{item['reason']} {item['rectangle']}" for item in excluded)
    if regions is not None:
        reason += f" in regions {[list(value) for value in rectangles]}"
    elif region is not None:
        reason += f" in region {list(rectangles[0])}"
    if channel_limit:
        reason += f" with maxChannelDifference={channel_limit}"
    if radius:
        reason += f" with blurRadius={radius}"
    return (different == 0 if tolerance == 1 else similarity >= tolerance), similarity, reason


class ScreenshotComparisons:
    """Batch-owned workers; only collect() on the caller thread changes results."""

    def __init__(self):
        self._executor = ThreadPoolExecutor(max_workers=2, thread_name_prefix="journal-compare")
        self._jobs = []
        self._summaries = {}
        self._paths = set()

    @staticmethod
    def _compare(expected, actual, diff, preparation_error, exclude_import_progress):
        comparison = dict(status="failed", mode="exact", differentPixels=None)
        evidence = [actual]
        try:
            if preparation_error:
                raise ValueError(preparation_error)
            evidence = [expected, actual]
            passed, similarity, reason = compare_screenshots(
                expected, actual.read_bytes(), 1.0, diff, details=comparison, exclude_fps=True,
                exclude_import_progress=exclude_import_progress)
            comparison.update(status="passed" if passed else "failed", similarity=similarity)
            evidence.append(diff)
            message = f"Full-image exact comparison: {reason}; differentPixels={comparison['differentPixels']}"
        except Exception as exc:
            message = f"Screenshot comparison failed: {exc}"
        return dict(status=comparison['status'], comparison=comparison, message=message, evidence=evidence)

    def prepare(self, journal_path, output, payload, index, actual):
        expected = output / "screenshots" / f"screenshot_{index:03d}_expected.png"
        diff = output / "screenshots" / f"screenshot_{index:03d}_diff.png"
        paths = {path.resolve() for path in (expected, actual, diff)}
        if len(paths) != 3 or self._paths.intersection(paths):
            raise ValueError("Screenshot output collides with pending comparison evidence")
        self._paths.update(paths)
        error = None
        exclude_import_progress = False
        try:
            baseline = payload.get("baseline")
            if not isinstance(baseline, str) or not baseline:
                raise ValueError("Screenshot has no approved baseline")
            # Freeze the standard before the next scene starts. Image decoding
            # and all pixel work happen exclusively in the worker.
            baseline_path = (journal_path.parent / baseline).resolve()
            exclude_import_progress = _native_import_progress_baseline(baseline_path)
            expected.write_bytes(baseline_path.read_bytes())
        except Exception as exc:
            error = str(exc)
        return expected, diff, error, exclude_import_progress

    def submit(self, output, prepared, actual, result):
        expected, diff, error, exclude_import_progress = prepared
        result.update(status="pending", message="Screenshot comparison queued",
                      comparison=dict(status="pending", mode="exact"), evidence=[actual])
        future = self._executor.submit(self._compare, expected, actual, diff, error, exclude_import_progress)
        self._jobs.append((future, result, str(output.resolve())))

    def track(self, summary):
        if any(step.get("status") == "pending" for step in summary["results"]):
            self._summaries[str(Path(summary["outputPath"]).resolve())] = summary

    def collect(self):
        """Apply completed work without waiting; return changed execution outputs."""
        changed = set()
        for job in self._jobs[:]:
            future, result, output = job
            if not future.done():
                continue
            try:
                result.update(future.result())
            except Exception as exc:
                result.update(status="failed", message=f"Screenshot worker failed: {exc}",
                              comparison=dict(status="failed", mode="exact", error=str(exc)))
            self._jobs.remove(job)
            changed.add(output)
        for output in changed:
            if output in self._summaries:
                finish_result(self._summaries[output])
                if not any(step.get("status") == "pending" for step in self._summaries[output]["results"]):
                    del self._summaries[output]
        return changed

    def close(self):
        """The batch/standalone owner calls this only after replay has ended."""
        self._executor.shutdown(wait=True)
        return self.collect()


def _result_path(value, directory):
    """Persist internal artifacts relative to their result file; keep external references."""
    directory = Path(directory).resolve()
    absolute = (directory / value).resolve()
    try:
        return absolute.relative_to(directory).as_posix()
    except ValueError:
        return str(absolute)


def write_result(path, summary):
    """Only the on-disk copy uses relative paths; live GUI/CLI results are unchanged."""
    path = Path(path)
    saved = copy.deepcopy(summary)
    for key in ("outputPath", "sessionPath", "reportPath", "applicationLogPath", "applicationLiveLogPath"):
        if saved.get(key):
            saved[key] = _result_path(saved[key], path.parent)
    for result in saved.get("results", []):
        result["evidence"] = [_result_path(item, path.parent) for item in result.get("evidence", [])]
    path.write_text(json.dumps(saved, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8")


def write_report(path, summary):
    journal_path, results = summary["journalPath"], summary["results"]
    passed = sum(1 for result in results if result["status"] == "passed")
    failed = sum(1 for result in results if result["status"] == "failed")
    skipped = sum(1 for result in results if result["status"] == "skipped")
    checked = sum(1 for result in results if result["status"] == "passed" and
                  (result["kind"].startswith("verify.") or result.get("comparison", {}).get("status") == "passed"))
    captures = sum(1 for result in results if result["status"] == "passed" and
                   (result["kind"].startswith("capture.") or
                    (result.get("actionCode") == "screenshot.capture" and
                     result.get("comparison", {}).get("status") != "passed")))
    rows = []
    for result in results:
        links = []
        for item in result.get("evidence", []):
            reference = _result_path(item, path.parent)
            external = Path(reference).is_absolute()
            url = Path(reference).as_uri() if external else quote(reference, safe="/")
            label = "external: " + reference if external else reference
            links.append('<a href="{}">{}</a>'.format(
                html.escape(url, quote=True), html.escape(label)))
        evidence = "<br>".join(links)
        rows.append(
            "<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>".format(
                result["index"], html.escape(result["kind"]),
                result["atMs"], html.escape(result["status"]),
                html.escape(result.get("message", "")) +
                ("<br>" + evidence if evidence else "")))
    content = """<!doctype html><meta charset="utf-8">
<title>Journal verification report</title>
<style>body{{font-family:sans-serif;margin:24px}}table{{border-collapse:collapse;width:100%}}
td,th{{border:1px solid #bbb;padding:6px;text-align:left}}.failed{{color:#b00020}}</style>
<h1>Journal verification report</h1>
<p>Execution: {} &nbsp; Acceptance: {}</p><p>{}</p>
<p>Journal: {}</p><p>Steps passed: {} &nbsp; Failed: {} &nbsp; Skipped: {}</p>
<p>Verification checks passed: {} &nbsp; Data captures (not verification): {}</p>
<table><thead><tr><th>#</th><th>Kind</th><th>atMs</th><th>Status</th><th>Result</th></tr></thead>
<tbody>{}</tbody></table>""".format(
        "passed" if summary.get("success") else "failed",
        html.escape(summary.get("acceptance", "not_verified")),
        html.escape("; ".join([summary.get("error", "")] + summary.get("cleanupErrors", []))),
        html.escape(str(journal_path)), passed, failed, skipped, checked, captures, "".join(rows))
    path.write_text(content, encoding="utf-8")
    return path


def finish_result(summary):
    """Write one authoritative result after the owning workflow has finished."""
    output = Path(summary["outputPath"])
    results = summary["results"]
    success = (bool(results) and not summary.get("error") and
               not summary.get("cleanupErrors") and
               len(results) == summary.get("totalEntries", len(results)) and
               not any(item["status"] in ("failed", "not_run") for item in results))
    validations = [item for item in results if item["kind"].startswith("verify.") or
                   item.get("comparison", {}).get("status") in ("pending", "passed", "failed")]
    pending = any(item["status"] == "pending" for item in results)
    disabled = any(item.get("comparison", {}).get("status") == "skipped" for item in results)
    summary["success"] = bool(success)
    summary["acceptance"] = ("failed" if not success else "pending" if pending else "passed"
                             if not disabled and validations and all(item["status"] == "passed" for item in validations)
                             else "not_verified")
    summary["completedEntries"] = len(results)
    summary["validationEntries"] = len(validations)
    summary["sessionPath"] = str(output)
    summary["reportPath"] = str(output / "verification_report.html")
    write_report(Path(summary["reportPath"]), summary)
    write_result(output / "execution_result.json", summary)
    return summary


class JournalRunner:
    """Owns Journal files, fixed-time replay, validation and reports."""

    def __init__(self, client, *, comparisons=None):
        self.client = client
        self.comparisons = comparisons
        self.session_path = None
        self._checkpoints = []
        self.recording = False
        self._recording_id = None
        self._pending_recording = None
        self._step_state = None
        self._step_path = None

    @property
    def has_pending_save(self):
        return self._pending_recording is not None

    def reconnect(self, client):
        """Keep local checkpoints and only reattach to the same live recording."""
        status = client.recording_status()
        if self.recording:
            if (not status.get("recording") or
                    status.get("recordingId") != self._recording_id):
                raise RuntimeError(
                    "应用中的录制已结束或已更换；本地会话和检查点仍保留，不能接入另一场录制")
        elif status.get("recording"):
            raise RuntimeError("应用正在录制，请使用发起录制的 GUI 窗口重新连接")
        self.client = client

    def start_recording(self, record_mouse_moves=True):
        if self.recording:
            raise RuntimeError("A Journal recording is already active")
        if self.has_pending_save:
            raise RuntimeError("Save the pending recording before starting another")
        paths = self.client.paths()
        session_path = Path(paths["recordings"]) / (
            f"session_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')[:-3]}")
        # Prepare local storage before asking the application to record.
        (session_path / "events").mkdir(parents=True, exist_ok=False)
        (session_path / "screenshots").mkdir()
        result = self.client.start_recording(record_mouse_moves, window_profile=WINDOW_PROFILE)
        try:
            wait_for_window(self.client)
        except Exception:
            self.client.stop_recording()
            raise
        self.session_path = session_path
        self._recording_id = result["recordingId"]
        self._checkpoints = []
        self.recording = True
        return self.session_path

    def record_db(self, db_instance_id, label=""):
        if not self.recording:
            raise RuntimeError("Start recording before adding a DB checkpoint")
        result = self.client.serialize_db(db_instance_id)
        if result.get("serializationFormat") != DB_SERIALIZATION_FORMAT:
            raise RuntimeError(
                "Application returned an unsupported DB serialization format")
        serialized = result.get("serialized")
        compare_serialized_data(serialized, serialized)
        entry = self.client.entry(
            "verify.db",
            {
                "dbInstanceId": self.client.normalize_db_id(db_instance_id),
                "label": label,
                "serializationFormat": DB_SERIALIZATION_FORMAT,
                "expectedSerialized": serialized,
            },
            result["atMs"], result["sequence"])
        self._checkpoints.append(entry)
        return entry

    def record_screenshot(self, label, tolerance=1.0):
        if not self.recording or self.session_path is None:
            raise RuntimeError("Start recording before adding a screenshot checkpoint")
        result = self.client.capture_screenshot()
        safe_label = "".join(character if character.isalnum() or character in "._-"
                             else "_" for character in label).strip("._") or "checkpoint"
        filename = f"baseline_{int(result['sequence']):03d}_{safe_label}.png"
        path = self.session_path / "screenshots" / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(result["png"])
        entry = self.client.entry(
            "verify.screenshot",
            {"label": label, "baseline": f"../screenshots/{filename}",
             "tolerance": float(tolerance)},
            result["atMs"], result["sequence"])
        self._checkpoints.append(entry)
        return entry

    def invoke_action(self, code, params=None):
        return self.client.invoke_action(code, params)

    def stop_recording(self, metadata=None):
        if self.has_pending_save:
            return self._save_pending_recording()
        if not self.recording:
            raise RuntimeError("No Journal recording is active")
        session_path = self.session_path
        if session_path is None:
            raise RuntimeError("Recording session path is unavailable")
        result = self.client.stop_recording()
        entries = list(result["entries"]) + self._checkpoints
        if not entries:
            self.recording = False
            self._recording_id = None
            raise JournalFormatError("Recording contains no entries to save")
        self._pending_recording = {
            "path": session_path,
            "entries": copy.deepcopy(entries),
            "metadata": copy.deepcopy(metadata or {}),
        }
        self.recording = False
        return self._save_pending_recording()

    def _save_pending_recording(self):
        pending = self._pending_recording
        journal_path = write_journal(
            pending["path"] / "events" / "Journal.json",
            pending["entries"])
        JournalSessionRepository().save_metadata(pending["path"], pending["metadata"])
        # Clear only after both the Journal and its session metadata were saved.
        self._pending_recording = None
        self._checkpoints = []
        self._recording_id = None
        return journal_path

    def _new_output(self, output_dir=None):
        paths = self.client.paths() if output_dir is None else {}
        return JournalSessionRepository(playbacks=paths.get("playbacks")).create_playback(output_dir)

    def _execute_entry(self, journal_path, output, entry, index, validate):
        result = {"index": index, "kind": entry["kind"],
                  "atMs": entry["atMs"], "status": "passed",
                  "message": "executed", "evidence": []}
        try:
            if entry["kind"].startswith("input.") or entry["kind"] == "action":
                if entry["kind"] == "action":
                    result["actionCode"] = entry["payload"]["code"]
                dispatched = entry
                screenshot_path = None
                screenshot_standard = None
                if (entry["kind"] == "action" and
                        entry["payload"].get("code") == "file.import"):
                    filepath = entry["payload"].get("params", {}).get("filePath")
                    if (isinstance(filepath, str) and filepath and
                            not Path(filepath).is_absolute()):
                        fixture_path = (journal_path.parent / filepath).resolve()
                        if not fixture_path.is_file():
                            raise FileNotFoundError(
                                f"Import fixture does not exist: {fixture_path}")
                        dispatched = copy.deepcopy(entry)
                        dispatched["payload"]["params"]["filePath"] = str(fixture_path)
                if entry["kind"] == "action" and entry["payload"].get("code") == "screenshot.capture":
                    filepath = entry["payload"].get("params", {}).get("filepath")
                    screenshot_path = (output / (filepath or f"screenshots/screenshot_{index:03d}_actual.png")).resolve()
                    if screenshot_path.exists():
                        raise FileExistsError(f"Screenshot already exists: {screenshot_path}")
                    screenshot_path.parent.mkdir(parents=True, exist_ok=True)
                    dispatched = copy.deepcopy(entry)
                    dispatched["payload"].setdefault("params", {})["filepath"] = str(screenshot_path)
                    # Baseline is a Python executor binding, not an App Action parameter.
                    dispatched["payload"].pop("baseline", None)
                    if validate:
                        screenshot_standard = self.comparisons.prepare(
                            journal_path, output, entry["payload"], index, screenshot_path)
                self.client.execute_entry(dispatched)
                if screenshot_path is not None:
                    if not screenshot_path.is_file() or screenshot_path.stat().st_size == 0:
                        raise RuntimeError(f"Screenshot Action did not save an image: {screenshot_path}")
                    result["evidence"].append(screenshot_path)
                    if validate:
                        self.comparisons.submit(output, screenshot_standard, screenshot_path, result)
                    else:
                        result.update(message="Candidate screenshot saved; not verification",
                                      comparison=dict(status="skipped"))
            elif entry["kind"] == "capture.db":
                data = self.client.serialize_db(entry["payload"]["dbInstanceId"])
                raw = base64.b64decode(data["serialized"], validate=True)
                actual_path = output / "reports" / f"db_{index:03d}_actual.bin"
                result["evidence"].append(actual_path)
                actual_path.write_bytes(raw)
                metadata_path = actual_path.with_suffix(".json")
                metadata_path.write_text(json.dumps({
                    "dbInstanceId": data["dbInstanceId"],
                    "serializationFormat": data["serializationFormat"],
                    "byteCount": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                result["evidence"].append(metadata_path)
                result["message"] = "raw DB data captured; no baseline comparison"
            elif not validate:
                result["status"] = "skipped"
                result["message"] = "validation disabled by caller"
            elif entry["kind"] == "verify.db":
                actual_result = self.client.serialize_db(
                    entry["payload"]["dbInstanceId"])
                expected_format = entry["payload"]["serializationFormat"]
                actual_format = actual_result.get("serializationFormat")
                if actual_format != expected_format:
                    raise RuntimeError(
                        "DB serialization format mismatch: "
                        f"{expected_format!r} != {actual_format!r}")
                comparison, expected, actual = compare_serialized_data(
                    entry["payload"]["expectedSerialized"],
                    actual_result.get("serialized"))
                stem = f"db_{index:03d}"
                expected_path = output / "reports" / f"{stem}_expected.bin"
                actual_path = output / "reports" / f"{stem}_actual.bin"
                comparison_path = output / "reports" / f"{stem}_comparison.json"
                expected_path.write_bytes(expected)
                actual_path.write_bytes(actual)
                comparison_path.write_text(
                    json.dumps(comparison, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
                result["evidence"] = [
                    expected_path, actual_path, comparison_path]
                if not comparison["equal"]:
                    result["status"] = "failed"
                    result["message"] = (
                        "DB serialized data mismatch: first byte difference at "
                        f"offset {comparison['firstDifferenceOffset']}")
                else:
                    result["message"] = "complete TransDB state matches"
            elif entry["kind"] == "verify.screenshot":
                actual_path = (
                    output / "screenshots" / f"screenshot_{index:03d}_actual.png")
                standard = self.comparisons.prepare(journal_path, output, entry["payload"], index, actual_path)
                capture = self.client.capture_screenshot()
                actual_path.write_bytes(capture["png"])
                self.comparisons.submit(output, standard, actual_path, result)
        except Exception as exc:
            result["status"] = "failed"
            result["message"] = str(exc)
        return result

    def run(self, document, output_dir, progress=None, validate=True, cancel_event=None):
        """Execute an already loaded Journal; the owner finalizes its result."""
        output = Path(output_dir)
        journal_path = Path(document["journalPath"])
        summary = {
            "journalPath": str(journal_path),
            "journalSha256": document["journalSha256"],
            "totalEntries": len(document["entries"]),
            "outputPath": str(output), "results": [],
        }
        owned_comparisons = self.comparisons is None
        if owned_comparisons:
            self.comparisons = ScreenshotComparisons()
        started = time.monotonic()
        try:
            for index, entry in enumerate(document["entries"], start=1):
                if cancel_event is not None and cancel_event.is_set():
                    raise InterruptedError("Replay cancelled by caller")
                scheduled = entry["atMs"] / 1000.0
                now = time.monotonic()
                remaining = scheduled - (now - started)
                if remaining > 0:
                    if cancel_event is not None:
                        if cancel_event.wait(remaining):
                            raise InterruptedError("Replay cancelled by caller")
                    else:
                        time.sleep(remaining)
                else:
                    # A slow capture/check must not consume later input gaps.
                    # Resume the recorded cadence instead of bursting to catch up.
                    started = now - scheduled
                if progress:
                    progress(index, len(document["entries"]), entry)
                result = self._execute_entry(journal_path, output, entry, index, validate)
                summary["results"].append(result)
                if result["status"] == "failed":
                    break
        except Exception as exc:
            summary["error"] = str(exc)
        finally:
            if owned_comparisons:
                self.comparisons.close()
                self.comparisons = None
            else:
                self.comparisons.track(summary)
        return summary

    def play(self, journal_path, validate=True, progress=None, *, restart=True, output_dir=None):
        self.cancel_steps()
        document = load_journal(journal_path)
        output = self._new_output(output_dir)
        summary = {
            "journalPath": document["journalPath"],
            "journalSha256": document["journalSha256"],
            "totalEntries": len(document["entries"]),
            "outputPath": str(output), "results": [],
        }
        prepared = False
        actual_window = None
        try:
            if restart:
                self.client.begin_replay(window_profile=WINDOW_PROFILE)
                prepared = True
                actual_window = wait_for_window(self.client)
            summary = self.run(document, output, progress, validate)
            if actual_window is not None:
                summary["actualWindow"] = actual_window
        except Exception as exc:
            summary["error"] = str(exc)
        finally:
            if prepared:
                try:
                    self.client.end_replay()
                except Exception as exc:
                    summary.setdefault("cleanupErrors", []).append(str(exc))
        return finish_result(summary)

    def begin_steps(self, journal_path, validate=True):
        self.cancel_steps()
        document = load_journal(journal_path)
        output = self._new_output()
        summary = {
            "journalPath": document["journalPath"],
            "journalSha256": document["journalSha256"],
            "totalEntries": len(document["entries"]),
            "outputPath": str(output), "results": [],
        }
        try:
            self.client.begin_replay(window_profile=WINDOW_PROFILE)
            try:
                summary["actualWindow"] = wait_for_window(self.client)
            except Exception:
                self.client.end_replay()
                raise
        except Exception as exc:
            summary["error"] = str(exc)
            finish_result(summary)
            raise
        self._step_state = {
            "document": document, "output": output,
            "validate": validate, "index": 0, "summary": summary,
            "ownedComparisons": self.comparisons is None,
        }
        if self.comparisons is None:
            self.comparisons = ScreenshotComparisons()
        self._step_path = Path(document["journalPath"])
        return document

    def step_journal(self, journal_path, validate=True):
        resolved = Path(journal_path).resolve()
        if self._step_state is None or resolved != self._step_path:
            self.begin_steps(resolved, validate)
        return self.step()

    def step(self):
        if self._step_state is None:
            raise RuntimeError("No step replay is active")
        state = self._step_state
        entries = state["document"]["entries"]
        offset = state["index"]
        entry = entries[offset]
        result = self._execute_entry(
            Path(state["document"]["journalPath"]), state["output"], entry,
            offset + 1, state["validate"])
        state["summary"]["results"].append(result)
        state["index"] += 1
        complete = state["index"] >= len(entries) or result["status"] == "failed"
        response = {"entry": result, "complete": complete,
                    "position": state["index"], "total": len(entries)}
        if complete:
            try:
                self.client.end_replay()
            except Exception as exc:
                state["summary"].setdefault("cleanupErrors", []).append(str(exc))
            finally:
                self._step_state = None
                self._step_path = None
            if state["ownedComparisons"]:
                self.comparisons.close()
                self.comparisons = None
            response["summary"] = finish_result(state["summary"])
        return response

    def cancel_steps(self):
        if self._step_state is None:
            return
        state = self._step_state
        state["summary"]["error"] = "Step replay cancelled before completion"
        try:
            self.client.end_replay()
        except Exception as exc:
            state["summary"].setdefault("cleanupErrors", []).append(str(exc))
        finally:
            self._step_state = None
            self._step_path = None
        if state["ownedComparisons"]:
            self.comparisons.close()
            self.comparisons = None
        finish_result(state["summary"])


def main():
    from journal_execution_session import JournalExecutionSession
    parser = argparse.ArgumentParser(description="Run a Journal through the shared execution session")
    parser.add_argument("--run", required=True, type=Path, help="Journal JSON file")
    parser.add_argument("--app", type=Path, help="Launch this executable as a dedicated test instance")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, help="Default: available port with --app, otherwise 9999")
    parser.add_argument("--output", type=Path, help="New result directory (must not exist)")
    parser.add_argument("--keep-app", action="store_true", help="Keep this session's app open after testing")
    args = parser.parse_args()
    try:
        result = JournalExecutionSession().run(
            args.run, application=args.app, host=args.host,
            port=args.port if args.port is not None else (0 if args.app else 9999),
            output_dir=args.output, keep_app=args.keep_app)
        print(json.dumps(result, ensure_ascii=False, indent=2, default=str))
        return 0 if result["acceptance"] == "passed" else (1 if not result["success"] else 2)
    except Exception as exc:
        print(json.dumps({"success": False, "error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
