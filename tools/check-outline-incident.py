#!/usr/bin/env python3
"""Independent, intentionally narrow logical oracle for the outline incident.

Never sends controller commands. CLI mode is for source-reviewed HardwareDisabled
--export-gcode binaries only. Application inputs/outputs/settings use temporary directories; HOME is preserved.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

MAX_BYTES = 65536
WORD = re.compile(r"([A-Z])([+-]?(?:\d+(?:\.\d*)?|\.\d+))")
HEX = re.compile(r"[0-9a-f]{64}\Z")


class Rejected(ValueError):
    pass


def require(condition, code):
    if not condition:
        raise Rejected(code)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    sha = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def bounded_read(path):
    with open(path, "rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    require(len(data) <= MAX_BYTES, "input_size_limit")
    return data


def numeric_values(record, keys, expected):
    values = [record[key] for key in keys]
    return all(type(value) in (int, float) and math.isfinite(value) for value in values) and values == expected


def inspect_project(data):
    """Check saved fixture intent, without a product loader/planner/encoder."""
    require(len(data) <= MAX_BYTES, "project_size_limit")
    try:
        project = json.loads(data)
        require(project["format"] == "blazer-studio", "project_format")
        require(project["version"] == 12, "project_version")
        require(project["units"] == "mm" and project["origin"] == "bottom-left", "project_coordinates")
        require(project["activePage"] == 0 and len(project["objects"]) == 1, "project_objects")
        art = project["objects"][0]
        require(art["kind"] == "rect" and art["visible"] is True and art["page"] == 0, "rectangle_role")
        require(numeric_values(art, ("x", "y", "width", "height"), [0, 0, 200, 150]), "rectangle_bounds")
        require(art["layer"] == 0 and art["toolLayer"] == 0 and art["engraveOutline"] is True, "outline_intent")
        require(not art["extraOperations"] and not art["settings"] and not art["path"] and
                not art["svg"] and not art["depthMap"], "unexpected_geometry_or_overrides")
        layers = project["layers"]
        require(layers[0]["mode"] == "Gravur", "saved_operation")
        require(layers[0]["enabled"] is True, "saved_output_disabled")
        require(all(layer["enabled"] is False for layer in layers[1:]), "extra_output_layer")
        for record in (layers[0], project["material"]["engrave"]):
            require(record["enabled"] is True and
                    numeric_values(record, ("power", "speed", "passes"), [100, 5000, 1]), "saved_parameters")
        require(project["material"]["cut"]["enabled"] is False, "saved_cut_enabled")
        profile = project["deviceProfile"]
        require(profile["workspace"] == {"width": 200, "height": 150, "origin": "bottom-left", "units": "mm"}, "workspace")
        require(profile["driver"]["id"] == "grbl-serial" and
                profile["driver"]["settings"]["powerScale"] == 1000, "power_scale")
        require("placement" not in project, "unexpected_placement")
    except (KeyError, IndexError, TypeError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise Rejected("malformed_project") from error
    return {"sourceSha256": digest(data), "boundsMm": [0, 0, 200, 150],
            "savedPowerPercent": 100, "savedFeedMmMin": 5000, "savedPasses": 1}


def words(line):
    # Strict comments and words: unrecognized syntax cannot disappear silently.
    clean, comment = "", False
    for char in line:
        if char == ";" and not comment:
            break
        if char == "(":
            require(not comment, "nested_comment")
            comment = True
        elif char == ")":
            require(comment, "unmatched_comment")
            comment = False
        elif not comment:
            clean += char
    require(not comment, "unclosed_comment")
    result, offset = [], 0
    while offset < len(clean):
        if clean[offset].isspace():
            offset += 1
            continue
        match = WORD.match(clean, offset)
        require(match is not None, "unknown_word_syntax")
        value = float(match[2])
        require(math.isfinite(value), "nonfinite_word")
        result.append((match[1], value))
        offset = match.end()
    return result


def check_gcode(data):
    require(0 < len(data) <= MAX_BYTES, "gcode_size_limit")
    try:
        lines = data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise Rejected("nonascii_gcode") from error
    require(len(lines) <= 128 and all(len(line) <= 2048 for line in lines), "gcode_work_limit")
    units = distance = coordinate = feedmode = plane = motion = None
    spindle, power, feed = None, None, None
    position = None  # Deliberately no assumed initial head position.
    first_xy = first_enable = None
    enable_requests = []
    requests, edges = [], []
    ended = False
    executable = 0
    for index, line in enumerate(lines):
        block = words(line)
        if not block:
            continue
        require(not ended, "code_after_program_end")
        executable += 1
        if executable == 1:
            require(block == [("M", 5.)], "initial_output_off_missing")
        scalars = {}
        groups = set()
        for letter, value in block:
            require(letter in "GMSFXY", "unsupported_axis_or_word")
            if letter not in "GM":
                require(letter not in scalars, "duplicate_word")
                scalars[letter] = value
            elif letter == "G":
                require(value in (0, 1, 17, 20, 21, 54, 90, 91, 94), "unsupported_gcode")
                group = ("motion" if value in (0, 1) else "units" if value in (20, 21)
                         else "distance" if value in (90, 91) else str(value))
                require(group not in groups, "conflicting_modal_group")
                groups.add(group)
                if value in (0, 1): motion = value
                elif value in (20, 21): units = value
                elif value in (90, 91): distance = value
                elif value == 54: coordinate = value
                elif value == 94: feedmode = value
                elif value == 17: plane = value
            else:
                require(value in (2, 3, 4, 5, 30), "unsupported_mcode_or_accessory")
                if value in (3, 4, 5):
                    require("spindle" not in groups, "conflicting_spindle_mode")
                    groups.add("spindle")
                    spindle = value
                    if value in (3, 4):
                        enable_requests.append(value)
                        if first_enable is None:
                            first_enable = index
                else:
                    ended = True
        if "S" in scalars:
            power = scalars["S"]
            require(0 <= power <= 1000, "power_out_of_range")
        if "F" in scalars:
            feed = scalars["F"]
        if "X" not in scalars and "Y" not in scalars:
            continue
        require(units == 21 and distance == 90 and coordinate == 54 and feedmode == 94 and plane == 17,
                "unexpected_motion_coordinate_contract")
        require(motion in (0, 1), "missing_motion_mode")
        if first_xy is None:
            first_xy = index
            require(motion == 0 and spindle == 5 and power == 0, "first_xy_not_output_off_rapid")
            require(scalars.get("X") == 0 and scalars.get("Y") == 0, "first_xy_not_explicit_figure_start")
            position = (0., 0.)
            target = position
        else:
            target = (scalars.get("X", position[0]), scalars.get("Y", position[1]))
        require(0 <= target[0] <= 200 and 0 <= target[1] <= 150, "outside_rectangle_envelope")
        requests.append((motion, position, target))
        if len(requests) > 1:
            require(motion == 1 and spindle == 4 and power == 1000 and feed == 5000,
                    "unexpected_travel_or_power_feed")
            require(target != position, "zero_length_powered_request")
            edges.append((position, target))
        position = target
    require(first_xy is not None and first_enable is not None and first_xy < first_enable,
            "output_enabled_before_approach")
    require(enable_requests == [4], "unexpected_output_enable_requests")
    require(len(requests) == 5 and len(edges) == 4, "not_exactly_four_edges")
    expected = {frozenset((a, b)) for a, b in (
        ((0, 0), (200, 0)), ((200, 0), (200, 150)),
        ((200, 150), (0, 150)), ((0, 150), (0, 0)))}
    require({frozenset(edge) for edge in edges} == expected, "unexpected_edge_geometry")
    require(all(edges[i][1] == edges[(i + 1) % 4][0] for i in range(4)), "contour_not_closed")
    length = sum(math.dist(a, b) for a, b in edges)
    require(math.isclose(length, 700, rel_tol=0, abs_tol=1e-9), "not_700mm")
    require(spindle == 5 and power == 0, "final_output_off_missing")
    return {"gcodeSha256": digest(data), "poweredEdges": 4, "poweredDistanceMm": length,
            "xyRequests": 5, "outputEnableRequests": 1, "outputMode": "M4", "firstXy": "output-off G0 X0 Y0", "fillRaster": False,
            "accessoryCommands": False, "finishReturn": False}


def export_offline(binary, expected_sha, project_bytes):
    require(HEX.fullmatch(expected_sha) is not None, "invalid_expected_binary_sha256")
    # Source-reviewed binary is copied, so a later rebuild cannot replace this run.
    with tempfile.TemporaryDirectory(prefix="blazer-outline-oracle-") as directory:
        root = Path(directory)
        copied_binary = root / "blazer-studio"
        shutil.copyfile(binary, copied_binary)
        copied_binary.chmod(0o700)
        require(file_digest(copied_binary) == expected_sha, "binary_sha256_mismatch")
        project = root / "input.blazer"
        project.write_bytes(project_bytes)  # Never pass the user's original file to the app.
        output = root / "output.gcode"
        environment = os.environ.copy()
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "QT_QPA_PLATFORMTHEME"):
            environment.pop(key, None)
        environment.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                           QML_DISABLE_DISK_CACHE="1", TMPDIR=str(root))
        for variable in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            target = root / variable.lower()
            target.mkdir(mode=0o700)
            environment[variable] = str(target)
        try:
            result = subprocess.run([str(copied_binary), "--open-project", str(project),
                                     "--export-gcode", str(output)], cwd=root, env=environment,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except subprocess.TimeoutExpired as error:
            raise Rejected("offline_cli_timeout") from error
        require(result.returncode == 0, "offline_cli_failed")
        require(project.read_bytes() == project_bytes, "temporary_input_changed")
        return bounded_read(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--binary", type=Path)
    source.add_argument("--gcode", type=Path)
    parser.add_argument("--expected-binary-sha256")
    args = parser.parse_args()
    if args.binary and not args.expected_binary_sha256:
        parser.error("--binary requires --expected-binary-sha256 for the reviewed HardwareDisabled binary")
    try:
        original = bounded_read(args.project)
        report = inspect_project(original)
        code = (export_offline(args.binary, args.expected_binary_sha256, original)
                if args.binary else bounded_read(args.gcode))
        report.update(check_gcode(code))
        require(bounded_read(args.project) == original, "original_changed_during_check")
        if args.binary:
            report["binarySha256"] = args.expected_binary_sha256
        report["logicalOnly"] = True
        report["coordinateAssumption"] = "G54 absolute mm; physical reference, WCO, signed envelope and airflow unverified"
        print(json.dumps(report, sort_keys=True))
        return 0
    except (Rejected, OSError) as error:
        print(json.dumps({"passed": False, "reason": str(error) if isinstance(error, Rejected) else "local_io_failure"}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
