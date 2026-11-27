import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/journal"))

from journal_runner import JournalFormatError, JournalRunner, load_journal, write_journal
from journalsystem import JournalSystem


class DocumentReplayTransportCheck(unittest.TestCase):
    def test_complete_v2_and_v3_documents_are_submitted_once(self):
        for version in (2, 3):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary)
                inputs = [JournalSystem.entry("action", {"code": "pilot.operation"}, sequence=2)]
                if version == 3:
                    inputs.append({"atMs": 0, "sequence": 1, "marker": "#pilot.expected"})
                path = write_journal(output / "Journal.json", inputs)
                client = Mock()
                native = {"status": "passed", "results": [
                    {"index": 1, "kind": "action", "atMs": 0, "status": "passed"}]}
                client.request.side_effect = [{"status": "running"}, native]
                result = JournalRunner(client).run(load_journal(path), output)
                self.assertEqual(result["results"][0]["status"], "passed")
                start, status = client.request.call_args_list
                self.assertEqual(start.args[0], "journal.script.start")
                submitted = json.loads(start.args[1]["document"])
                self.assertEqual(submitted, json.loads(path.read_text()))
                self.assertEqual(submitted["formatVersion"], version)
                if version == 3:
                    self.assertEqual(submitted["entries"][0], "#pilot.expected")
                self.assertEqual(status.args, ("journal.script.status", {"after": 0}))
                client.execute_entry.assert_not_called()

    def test_native_failure_and_groups_are_preserved_without_any_dispatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            path = write_journal(output / "Journal.json", [
                {"atMs": 0, "sequence": 1, "marker": "#fixture.whatever"},
                JournalSystem.entry("action", {"code": "must.not.run"}, sequence=2)])
            observation = [{"unexpected": {"#fixture.extra": 1}, "observed": ["#fixture.extra"]}]
            client = Mock()
            client.request.side_effect = [
                {"status": "running"},
                {"status": "failed", "results": [], "error": "Native signal group has unexpected signals",
                 "signalGroups": observation}]
            result = JournalRunner(client).run(load_journal(path), output)
            self.assertEqual(result["error"], "Native signal group has unexpected signals")
            self.assertEqual(result["signalGroups"], observation)
            self.assertEqual(result["results"], [])
            self.assertEqual(client.request.call_count, 2)
            client.execute_entry.assert_not_called()

    def test_only_valid_string_marks_are_accepted_in_v3(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "Journal.json"
            action = {"kind": "action", "atMs": 0, "payload": {"code": "expected.operation"}}
            wrapped = {"kind": "signal", "atMs": 0, "payload": {"name": "#fixture"}}
            for version, marker, error in [
                (2, "#fixture", "requires formatVersion 3"),
                (3, wrapped, "instead of a signal object"),
                (3, 42, "operation object or a #signal string"),
                (3, "#bad name", "and a #signal name"),
            ]:
                with self.subTest(version=version, marker=marker):
                    path.write_text(json.dumps({"formatVersion": version, "entries": [marker, action]}))
                    with self.assertRaisesRegex(JournalFormatError, error):
                        load_journal(path)

    def test_markers_do_not_change_checkpoint_number_or_candidate_filename(self):
        from journal_replay_evidence import runtime_document
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
        from acceptance import materialize_journal
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            authored = root / "Journal.json"
            operations = [
                "#fixture.ready",
                {"kind": "action", "atMs": 100, "payload": {"code": "example"}},
                "#fixture.completed", "#fixture.completed",
                {"kind": "verify.screenshot", "atMs": 110, "payload": {"baseline": "baseline.png"}},
                "#fixture.finished",
            ]
            authored.write_text(json.dumps({"formatVersion": 3, "entries": operations}))
            loaded = load_journal(authored)
            self.assertEqual([entry["sequence"] for entry in loaded["entries"]], [1, 2])
            case = {"journal": authored, "metadata": {}}
            path, _ = materialize_journal(case, root / "case", None, capture_candidates=True)
            materialized = json.loads(runtime_document(path.read_bytes(), root / "case"))["entries"]
            self.assertEqual([item for item in materialized if isinstance(item, str)],
                             [item for item in operations if isinstance(item, str)])
            self.assertEqual(materialized[4]["payload"]["params"]["filepath"],
                             str((root / "case/candidates/screenshot_002.png").resolve()))
