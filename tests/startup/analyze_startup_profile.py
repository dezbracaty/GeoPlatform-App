"""Summarize Qt .qtd ranges without adding nested calls twice.

Creating ranges include construction and completion work: invocation counts
are profiler events, not a count of live controls. Post-Ready account refresh
is excluded using its first recorded JavaScript invocation as the cutoff.
"""

import argparse
from collections import defaultdict
import csv
from difflib import SequenceMatcher
from functools import lru_cache
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def analyze(trace, app_root, build_root):
    tree = ET.parse(trace).getroot()
    metadata = {int(event.attrib["index"]): {child.tag: child.text or "" for child in event}
                for event in tree.find("eventData")}
    spans = []
    cutoff = int(tree.attrib["traceEnd"])
    for item in tree.find("profilerDataModel"):
        if "duration" not in item.attrib:
            continue
        start = int(item.attrib["startTime"])
        index = int(item.attrib["eventIndex"])
        spans.append([start, start + int(item.attrib["duration"]), index, 0])
        if metadata[index].get("details") == "refreshStartupAccount":
            cutoff = min(cutoff, start)
    spans = sorted(([start, min(end, cutoff), index, min(end, cutoff) - start]
                    for start, end, index, _ in spans if start < cutoff),
                   key=lambda span: (span[0], -span[1]))
    stack, crossing = [], 0
    for index, span in enumerate(spans):
        while stack and spans[stack[-1]][1] <= span[0]:
            stack.pop()
        if stack:
            parent = spans[stack[-1]]
            if span[1] <= parent[1]:
                parent[3] -= span[1] - span[0]
            else:
                crossing += 1
        stack.append(index)
    if crossing or any(span[3] < 0 for span in spans):
        raise ValueError("Non-nested profiler ranges require thread-aware analysis")

    @lru_cache(None)
    def source_map(url):
        source, generated = None, None
        prefix = "qrc:/qt/qml/GPlatform/"
        if url.startswith(prefix):
            relative = url[len(prefix):]
            source = app_root / relative
            generated = build_root / "GPlatform/protected_qml" / relative
        elif url.startswith("qrc:/gplatform/startup/"):
            source = app_root / "qml/startup" / url.rsplit("/", 1)[-1]
        else:
            for module in ("SmoothUI", "SmoothUIDock"):
                prefix = "qrc:/qt/qml/" + module + "/"
                if url.startswith(prefix):
                    source = app_root.parent / "ThirdParty" / module / url[len(prefix):]
                    break
        if source is None or not source.is_file():
            return None, {}
        if generated is None or not generated.is_file():
            return str(source), None
        original = [line.strip() for line in source.read_text().splitlines()]
        protected = [line.strip() for line in generated.read_text().splitlines()]
        mapping = {}
        for block in SequenceMatcher(None, protected, original, autojunk=False).get_matching_blocks():
            for offset in range(block.size):
                mapping[block.a + offset + 1] = block.b + offset + 1
        return str(source), mapping

    grouped = defaultdict(lambda: [0, 0, 0])
    for start, end, index, self_ns in spans:
        grouped[index][0] += end - start
        grouped[index][1] += self_ns
        grouped[index][2] += 1
    rows = []
    category_ns, file_ns = defaultdict(int), defaultdict(int)
    for index, (inclusive, self_ns, count) in grouped.items():
        event = metadata[index]
        url = event.get("filename", "")
        line = int(event.get("line", "0"))
        source, mapping = source_map(url)
        source_line = line if mapping is None else mapping.get(line)
        rows.append({"eventIndex": index, "type": event.get("type", ""),
                     "file": url, "traceLine": line, "source": source,
                     "sourceLine": source_line, "details": event.get("details", ""),
                     "calls": count, "inclusiveMs": inclusive / 1e6, "selfMs": self_ns / 1e6})
        category_ns[event.get("type", "unknown")] += self_ns
        file_ns[url or "<unknown>"] += self_ns
    rows.sort(key=lambda row: -row["selfMs"])
    # An independent union must equal the summed self times if all ranges nest.
    merged, union_ns = [], 0
    for start, end, _, _ in spans:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    union_ns = sum(end - start for start, end in merged)
    assert union_ns == sum(category_ns.values())
    return {"trace": str(trace), "rangeCount": len(spans), "crossingRanges": crossing,
            "traceStartNs": int(tree.attrib["traceStart"]), "startupCutoffNs": cutoff,
            "cutoff": "First refreshStartupAccount call, or trace end if absent",
            "coveredQmlTimeMs": union_ns / 1e6,
            "categorySelfMs": {key: value / 1e6 for key, value in category_ns.items()},
            "fileSelfMs": dict(sorted(((key, value / 1e6) for key, value in file_ns.items()),
                                      key=lambda row: -row[1])),
            "countMeaning": "Creation and completion events, not live object counts",
            "rows": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-root", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.trace.resolve(), args.app_root.resolve(), args.build_root.resolve())
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "analysis.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    with (args.output / "events.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(result["rows"][0]))
        writer.writeheader()
        writer.writerows(result["rows"])
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ("rows", "fileSelfMs")}, indent=2))


if __name__ == "__main__":
    main()
