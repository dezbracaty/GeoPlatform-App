import json
import os
import re
import shutil
import sys
import uuid
from datetime import datetime
from pathlib import Path


class JournalSessionRepository:
    """Filesystem operations for Journal sessions, independent of Tk widgets."""

    def __init__(self, recordings=None, playbacks=None, scenarios=None):
        self.recordings = Path(recordings) if recordings else None
        self.playbacks = Path(playbacks) if playbacks else None
        self.scenarios = Path(scenarios) if scenarios else None
        self.external_scenarios = set()

    @staticmethod
    def default_paths(application):
        """Offline GPlatform defaults; journal.paths overrides these after connection."""
        if sys.platform == "darwin":
            data = Path.home() / "Library/Application Support"
        elif sys.platform.startswith("win"):
            data = Path(os.environ.get("APPDATA") or Path.home() / "AppData/Roaming")
        else:
            data = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
        return {"recordings": data / "GPlatform/GPlatform/Journal/recordings",
                "playbacks": Path(application).resolve().parent / "Journal/playbacks"}

    def set_paths(self, recordings, playbacks):
        self.recordings = Path(recordings)
        self.playbacks = Path(playbacks)

    def list_sessions(self):
        sessions = []
        for root, session_type in (
            (self.recordings, "录制"), (self.playbacks, "回放"), (self.scenarios, "场景")):
            paths = set(root.glob("*" if session_type == "场景" else "session_*")) if root and root.is_dir() else set()
            if session_type == "场景":
                paths.update(self.external_scenarios)
            for path in sorted(paths):
                if not path.is_dir():
                    continue
                session = self.describe_session(path, session_type)
                if session_type == "场景" and session["journal_file"] is None:
                    continue
                sessions.append(session)
        return sorted(sessions, key=lambda item: item["mtime"], reverse=True)

    def describe_session(self, path, session_type):
        path = Path(path).resolve()
        journal = path / "events" / "Journal.json"
        journal = journal if journal.is_file() else None
        modified = (journal or path).stat().st_mtime
        metadata, error = {}, ""
        try:
            metadata = self.load_metadata(path)
            if not isinstance(metadata, dict):
                raise ValueError("metadata.json must be an object")
        except (OSError, ValueError) as exc:
            metadata, error = {}, str(exc)
        return {
            "name": metadata.get("name") or path.name,
            "path": path, "journal_file": journal, "type": session_type,
            "mtime": modified,
            "mtime_str": datetime.fromtimestamp(modified).strftime("%Y-%m-%d %H:%M:%S"),
            "metadata": metadata, "error": error,
        }

    def create_playback(self, output_dir=None):
        if output_dir is None:
            if self.playbacks is None:
                raise ValueError("Playback root is not configured")
            output = self.playbacks / (
                "session_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f") +
                "_" + uuid.uuid4().hex[:6])
        else:
            output = Path(output_dir)
        output = output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        (output / "reports").mkdir()
        (output / "screenshots").mkdir()
        return output

    def load_metadata(self, session):
        path = Path(session["path"] if isinstance(session, dict) else session)
        metadata_file = path / "metadata.json"
        if not metadata_file.exists():
            return {}
        with metadata_file.open("r", encoding="utf-8") as stream:
            return json.load(stream)

    def save_metadata(self, session, metadata):
        path = Path(session["path"] if isinstance(session, dict) else session)
        path.mkdir(parents=True, exist_ok=True)
        metadata_file = path / "metadata.json"
        with metadata_file.open("w", encoding="utf-8") as stream:
            json.dump(metadata, stream, ensure_ascii=False, indent=2)
        return metadata_file

    def rename(self, session, display_name):
        path = Path(session["path"] if isinstance(session, dict) else session)
        cleaned = "".join("_" if character in '<>:"/\\|?*' else character
                          for character in display_name).strip(" .")[:100]
        if not cleaned:
            raise ValueError("Session display name is empty")
        match = re.search(r"(\d{8}_\d{6}_\d{3})$", path.name)
        suffix = match.group(1) if match else datetime.fromtimestamp(
            path.stat().st_mtime).strftime("%Y%m%d_%H%M%S_000")
        destination = path.with_name(f"session_{cleaned}_{suffix}")
        if destination.exists():
            raise FileExistsError(destination)
        path.rename(destination)
        return destination

    def delete(self, session):
        path = Path(session["path"] if isinstance(session, dict) else session)
        if not path.name.startswith("session_"):
            raise ValueError(f"Refusing to delete non-session directory: {path}")
        shutil.rmtree(path)
