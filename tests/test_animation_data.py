#!/usr/bin/env python3
"""Validate authored eSheep animation data and prove reproducible generation.

Stdlib-only validator for tools/esheep_animations.xml (the authored
behavior graph) and the generated C tables it produces.  Run directly or
via `make test-animation-data`.

Checks on the authored XML:
- animation ids are integers forming exactly 1..N in document order,
  because the generated array is indexed as esheep_animations[id - 1];
- spawn ids are positive and unique (generated C arrays are named per id);
- frame indices are integers within 0 .. tilesx*tilesy - 1, at least one;
- transition probabilities are integers in 1..100, "only" contexts are
  ones the runtime actually passes, targets reference existing
  animations, and no transition is shadowed by earlier buckets that
  already sum to 100 in the same context (unreachable bucket);
- repeat/repeat_from, pose, spawn, and child expressions use only the
  forms the C evaluators (repeat_value, pose_value, eval_spawn_expression,
  eval_child_expression) actually support;
- child records reference existing parent and child animations.

In-memory fixtures exercise every failure mode so the checks are proven
to fire, not just vacuously true for the current XML.  A final test
regenerates the C tables into a disposable directory and compares them
byte for byte with the checked-in generated files.
"""
import math
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"e": "https://esheep.petrucci.ch/"}
NS_URI = "https://esheep.petrucci.ch/"
REPO_ROOT = Path(__file__).resolve().parent.parent

# Context strings the C runtime actually passes to transition rollers
# (src/main.c tick, border, and gravity calls).
RUNTIME_CONTEXTS = ("none", "window", "taskbar", "vertical", "horizontal+")

# Expression allowlists, mirrored from the C evaluators:
#  - repeat_value          (src/interpreter.c)
#  - pose_value            (src/main.c)
#  - eval_spawn_expression (src/main.c)
#  - eval_child_expression (src/main.c)
SPAWN_EXPRESSIONS = {
    "screenW",
    "screenW+10",
    "areaH-imageH",
    "areaH/2-imageH",
    "areaH/2",
    "-imageH-20",
    "areaH/2-(randS*areaH/2)/120-imageH",
}
# src/main.c matches this one with strstr, not strcmp.
SPAWN_SUBSTRING = "random*(screenW-imageW-50)/100+25"

CHILD_EXPRESSIONS = {
    "-imageW",
    "-imageW-8",
    "imageY",
    "imageH",
    "imageX",
    "imageX-imageW*0.9",
    "areaH-imageH",
    "screenW+10-areaH/2-(randS*areaH/2)/120",
}

REPEAT_EXPRESSIONS = {
    "(screenW/2)/30-6",
    "24+(Convert(screenW/2,System.Int32)%30)/7",
    "25+(Convert(screenW/2,System.Int32)%30)/7",
}
REPEAT_RANDOM_A = re.compile(r"random/([0-9]+)\+([0-9]+)")
REPEAT_RANDOM_B = re.compile(r"([0-9]+)\+random/([0-9]+)")
REPEAT_AREA = re.compile(r"\(areaH/2\+\(randS\*areaH/2\)/120-imageH-([0-9]+)\)/2")
IMAGE_FACTOR = re.compile(r"[+-]?image[WH]\*([0-9]+(?:\.[0-9]+)?)")

# Inventory copied from the original eSheep64 animations.xml source.  The
# names and IDs are part of the authored behavior graph, not display labels
# invented by this project.
ORIGINAL_ANIMATION_NAMES = {
    1: "walk", 2: "rotate1a", 3: "rotate1b", 4: "drag", 5: "fall",
    6: "fall fast", 7: "run", 8: "boing", 9: "fall soft", 10: "fall hard",
    11: "pissa", 12: "pissb", 13: "kill", 14: "sync", 15: "sleep1a",
    16: "sleep1b", 17: "sleep2a", 18: "sleep2b", 19: "sleep3a", 20: "sleep3b",
    21: "batha", 22: "bathb", 23: "bathw", 24: "bathz", 25: "jump",
    26: "eat", 27: "flower", 28: "blacksheepa", 29: "blacksheepb",
    30: "blacksheepc", 31: "blacksheepv", 32: "blacksheepw", 33: "blacksheepy",
    34: "blacksheepz", 35: "run_begin", 36: "run_end", 37: "vertical_walk_up",
    38: "top_walk", 39: "top_walk2", 40: "top_walk3", 41: "vertical_walk_down",
    42: "vertical_walk_over", 43: "look_down", 44: "jump_down", 45: "jump_down2",
    46: "jump_down3", 47: "bathc", 48: "bathd", 49: "walk_win2", 50: "walk_task2",
    51: "fall_wina", 52: "fall_winb", 53: "fall_winc", 54: "fall_wind",
}

# The original source has exactly these three multi-sprite compositions.  The
# expressions preserve absolute bath placement and relative flower/UFO
# placement.  The UFO keeps this checkout's validated -8 spacing adjustment.
ORIGINAL_CHILD_RECORDS = (
    (21, "screenW+10-areaH/2-(randS*areaH/2)/120", "areaH-imageH", 23),
    (26, "imageX-imageW*0.9", "imageY", 27),
    (28, "-imageW-8", "imageY", 31),
)

EXTENSION_ANIMATIONS = {
    55: ("face_turn_extension", (11, 14)),
    56: ("hand_to_mouth_extension", (22, 26, 27)),
    57: ("glasses_reaction_extension", (52, 53, 54, 55, 56, 57)),
    58: ("hand_wave_extension", (71, 72, 73, 74, 75)),
    59: ("tumble_extension", tuple(range(83, 96))),
    60: ("tumble_recover_extension", (99, 100, 101, 102)),
    61: ("meteorite_extension", tuple(range(109, 119))),
    62: ("comet_extension", tuple(range(120, 127))),
    63: ("falling_body_extension", (131, 132)),
    64: ("atmospheric_reentry", tuple(range(133, 146))),
    65: ("spacecraft_flight", tuple(range(158, 166))),
    66: ("spacecraft_pilot", (166, 167, 168)),
    67: ("grid_overlay_extension", (172,)),
}


def _is_int(value):
    return value is not None and re.fullmatch(r"[+-]?[0-9]+", value) is not None


def valid_repeat_expression(expr):
    if expr is None:
        return False
    expr = expr.strip()
    if _is_int(expr):
        return True
    if expr in REPEAT_EXPRESSIONS:
        return True
    match = REPEAT_RANDOM_A.fullmatch(expr)
    if match is not None and int(match.group(1)) > 0:
        return True
    match = REPEAT_RANDOM_B.fullmatch(expr)
    if match is not None and int(match.group(2)) > 0:
        return True
    return REPEAT_AREA.fullmatch(expr) is not None


def valid_pose_expression(expr):
    if expr is None:
        return False
    expr = expr.strip()
    if _is_int(expr):
        return True
    return IMAGE_FACTOR.fullmatch(expr) is not None


def test_original_animation_inventory():
    root = ET.parse(REPO_ROOT / "tools" / "esheep_animations.xml").getroot()
    actual = {
        int(an.get("id")): an.findtext("e:name", default="", namespaces=NS)
        for an in root.findall("e:animations/e:animation", NS)
        if int(an.get("id")) <= 54
    }
    assert actual == ORIGINAL_ANIMATION_NAMES, (
        "authored animation inventory drifted: "
        f"expected {len(ORIGINAL_ANIMATION_NAMES)} original records, got {len(actual)}"
    )
    assert set(actual) == set(range(1, 55))
    print(f"OK: original animation inventory has {len(actual)} records")


def test_original_child_inventory():
    root = ET.parse(REPO_ROOT / "tools" / "esheep_animations.xml").getroot()
    actual = []
    for child in root.findall("e:childs/e:child", NS):
        if int(child.get("animationid")) > 54:
            continue
        actual.append((
            int(child.get("animationid")),
            child.findtext("e:x", default="", namespaces=NS),
            child.findtext("e:y", default="", namespaces=NS),
            int(child.findtext("e:next", default="0", namespaces=NS)),
        ))
    assert tuple(actual) == ORIGINAL_CHILD_RECORDS
    print(f"OK: original child inventory has {len(actual)} compositions")


def test_art_completeness_extensions():
    root = ET.parse(REPO_ROOT / "tools" / "esheep_animations.xml").getroot()
    actual = {}
    for an in root.findall("e:animations/e:animation", NS):
        aid = int(an.get("id"))
        if aid >= 55:
            actual[aid] = (
                an.findtext("e:name", default="", namespaces=NS),
                tuple(int(frame.text) for frame in an.findall("e:sequence/e:frame", NS)),
            )
    assert actual == EXTENSION_ANIMATIONS
    assert tuple(
        (int(child.get("animationid")), int(child.findtext("e:next", namespaces=NS)))
        for child in root.findall("e:childs/e:child", NS)
        if int(child.get("animationid")) >= 55
    ) == ((65, 66),)
    spacecraft = root.find("e:childs/e:child[@animationid='65']", NS)
    assert spacecraft is not None
    assert spacecraft.findtext("e:x", namespaces=NS) == "0"
    assert spacecraft.findtext("e:y", namespaces=NS) == "imageH"
    print(f"OK: art-completeness inventory has {len(actual)} extension records")


def valid_spawn_expression(expr):
    if expr is None or expr == "":
        return False
    expr = expr.strip()
    if _is_int(expr):
        return True
    return expr in SPAWN_EXPRESSIONS or SPAWN_SUBSTRING in expr


def valid_child_expression(expr):
    if expr is None or expr == "":
        return False
    expr = expr.strip()
    if _is_int(expr):
        return True
    return expr in CHILD_EXPRESSIONS or valid_spawn_expression(expr)


def validate_xml_text(text, source="<memory>"):
    """Return a list of human-readable validation errors (empty means valid)."""
    errors = []

    def err(where, message):
        errors.append(f"{source} {where}: {message}")

    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        return [f"{source}: XML parse error: {exc}"]

    if root.tag != f"{{{NS_URI}}}animations":
        err("<root>", "root element must be <animations>")
        return errors

    # Tile grid, computed exactly the way the generator does (header lookup
    # with 16/11 fallbacks).
    header = root.find("e:header", NS)
    if header is None:
        err("<header>", "element missing (the generator requires it)")
    else:
        for name in ("tilesx", "tilesy"):
            el = header.find(f"e:{name}", NS)
            if el is not None and el.text is not None:
                raw = el.text.strip()
                if not _is_int(raw):
                    err(f"<header> {name}", f"value {raw!r} is not an integer")

    def header_int(name, default):
        el = header.find(f"e:{name}", NS) if header is not None else None
        if el is None or el.text is None:
            return default
        raw = el.text.strip()
        return int(raw) if _is_int(raw) else default

    tilesx = header_int("tilesx", 16)
    tilesy = header_int("tilesy", 11)
    tile_count = tilesx * tilesy

    def check_transitions(element, where, section_label):
        """Validate <next> children of a transition-bearing element."""
        transitions = []
        if element is None:
            return transitions
        for index, nxt in enumerate(element.findall("e:next", NS)):
            nwhere = f"{where} {section_label} <next> #{index + 1}"
            prob = None
            raw_prob = nxt.get("probability")
            if raw_prob is not None:
                raw_prob = raw_prob.strip()
                if _is_int(raw_prob):
                    prob = int(raw_prob)
                    if prob < 1:
                        err(nwhere, f"probability {prob} can never fire (must be 1..100)")
                    elif prob > 100:
                        err(nwhere, f"probability {prob} out of range 1..100")
                else:
                    err(nwhere, f"probability {raw_prob!r} is not an integer")
            only = nxt.get("only")
            if only is not None and only not in RUNTIME_CONTEXTS:
                err(nwhere, f"only={only!r} is never passed as a runtime context")
            raw_target = (nxt.text or "").strip()
            target = int(raw_target) if _is_int(raw_target) else None
            if target is None:
                err(nwhere, f"target {raw_target!r} is not an integer")
            transitions.append((prob, only, target, nwhere))
        # Bucket walk per runtime context: a transition is unreachable when
        # earlier matching buckets already sum to 100.  Partial coverage
        # (sum < 100) is legal; the runtime treats leftover rolls as
        # "no transition" (see animation 1's walk-keep handling).
        for context in RUNTIME_CONTEXTS:
            accumulated = 0
            for prob, only, _target, nwhere in transitions:
                if only is not None and only != context:
                    continue
                if prob is None:
                    continue
                if accumulated >= 100:
                    err(nwhere, f"unreachable in context {context!r}: earlier transitions already sum to 100")
                accumulated += prob
        return transitions

    animations = []
    anims_el = root.find("e:animations", NS)
    if anims_el is None:
        err("<animations>", "section missing (at least one animation is required)")
    else:
        for an in anims_el.findall("e:animation", NS):
            label = an.get("id", "?")
            where = f"<animation id={label}>"
            anim = {"id": None, "transitions": {}}
            if an.get("id") is None:
                err(where, "id attribute is missing")
            elif not _is_int(an.get("id")):
                err(where, f"id {an.get('id')!r} is not an integer")
            else:
                anim["id"] = int(an.get("id"))

            for pose_name in ("start", "end"):
                pose = an.find(f"e:{pose_name}", NS)
                if pose is None:
                    err(where, f"missing <{pose_name}> pose (the generator requires it)")
                    continue
                for field in ("x", "y"):
                    el = pose.find(f"e:{field}", NS)
                    if el is not None and el.text is not None and el.text.strip() != "":
                        value = el.text.strip()
                        if not valid_pose_expression(value):
                            err(where, f"<{pose_name}> <{field}> expression {value!r} is not supported by pose_value")
                for field, kind in (("interval", int), ("opacity", float)):
                    el = pose.find(f"e:{field}", NS)
                    if el is not None and el.text is not None and el.text.strip() != "":
                        raw = el.text.strip()
                        try:
                            parsed = kind(raw)
                        except ValueError:
                            err(where, f"<{pose_name}> <{field}> {raw!r} is not a {kind.__name__}")
                        else:
                            if kind is float and not math.isfinite(parsed):
                                err(where, f"<{pose_name}> <{field}> {raw!r} must be finite")
                el = pose.find("e:offsety", NS)
                if el is not None and el.text is not None and el.text.strip() != "":
                    raw = el.text.strip()
                    if not _is_int(raw):
                        err(where, f"<{pose_name}> <offsety> {raw!r} is not an integer (atoi at runtime)")

            seq = an.find("e:sequence", NS)
            if seq is None:
                err(where, "missing <sequence> (the generator requires it)")
            else:
                for attr in ("repeat", "repeatfrom"):
                    raw = seq.get(attr, "0")
                    if not valid_repeat_expression(raw):
                        err(where, f"<sequence {attr}={raw!r}> is not a supported repeat expression")
                frames = seq.findall("e:frame", NS)
                if not frames:
                    err(where, "sequence has no <frame> elements")
                for i, f in enumerate(frames):
                    raw_frame = (f.text or "").strip()
                    if not _is_int(raw_frame):
                        err(where, f"<frame> #{i + 1} value {raw_frame!r} is not an integer")
                    elif not 0 <= int(raw_frame) < tile_count:
                        err(where, f"<frame> #{i + 1} value {raw_frame} outside tile range 0..{tile_count - 1}")
                action = seq.find("e:action", NS)
                if action is not None and (action.text or "").strip() != "flip":
                    err(where, f"action {(action.text or '').strip()!r} is not supported")
                anim["transitions"]["sequence"] = check_transitions(seq, where, "sequence")
            anim["transitions"]["border"] = check_transitions(an.find("e:border", NS), where, "border")
            anim["transitions"]["gravity"] = check_transitions(an.find("e:gravity", NS), where, "gravity")
            animations.append(anim)

    # Spawn ids: generated C arrays are named spawn{id}_next, so ids must
    # be positive and unique.  Document order is not significant for spawns.
    spawn_ids = set()
    spawn_transitions = []
    spawns_el = root.find("e:spawns", NS)
    if spawns_el is not None:
        for sp in spawns_el.findall("e:spawn", NS):
            where = f"<spawn id={sp.get('id', '?')}>"
            raw_id = sp.get("id")
            if raw_id is None:
                err(where, "id attribute is missing")
            elif not _is_int(raw_id):
                err(where, f"id {raw_id!r} is not an integer")
            elif int(raw_id) < 1:
                err(where, f"id {raw_id} must be a positive integer")
            elif int(raw_id) in spawn_ids:
                err(where, f"duplicate spawn id {raw_id} (generated C arrays are named per id)")
            else:
                spawn_ids.add(int(raw_id))
            raw_prob = sp.get("probability", "100").strip()
            if not _is_int(raw_prob):
                err(where, f"probability {raw_prob!r} is not an integer")
            elif not 0 <= int(raw_prob) <= 100:
                err(where, f"probability {raw_prob} out of range 0..100")
            for field in ("x", "y"):
                el = sp.find(f"e:{field}", NS)
                value = (el.text or "").strip() if el is not None else "0"
                if not valid_spawn_expression(value):
                    err(where, f"<{field}> expression {value!r} is not supported by eval_spawn_expression")
            spawn_transitions.append((where, check_transitions(sp, where, "spawn next")))

    # Child records: parent and child animation references.
    child_refs = []
    childs_el = root.find("e:childs", NS)
    if childs_el is not None:
        for i, child in enumerate(childs_el.findall("e:child", NS)):
            where = f"<child> #{i + 1}"
            parent = None
            raw_parent = child.get("animationid")
            if raw_parent is None:
                err(where, "animationid attribute is missing")
            elif _is_int(raw_parent):
                parent = int(raw_parent)
            else:
                err(where, f"animationid {raw_parent!r} is not an integer")
            for field in ("x", "y"):
                el = child.find(f"e:{field}", NS)
                value = (el.text or "").strip() if el is not None else "0"
                if not valid_child_expression(value):
                    err(where, f"<{field}> expression {value!r} is not supported by eval_child_expression")
            nxt = child.find("e:next", NS)
            child_target = None
            if nxt is None or (nxt.text or "").strip() == "":
                err(where, "missing <next> child animation reference")
            elif _is_int((nxt.text or "").strip()):
                child_target = int(nxt.text.strip())
            else:
                err(where, f"<next> target {nxt.text.strip()!r} is not an integer")
            child_refs.append((where, parent, child_target))

    # Second pass: cross references that need the full id set.
    id_sequence = [a["id"] for a in animations if a["id"] is not None]
    for a in animations:
        if a["id"] is None:
            continue
        previous = [b for b in animations if b is not a and b["id"] == a["id"]]
        if previous:
            err(f"<animation id={a['id']}>", f"duplicate animation id {a['id']}")
    if animations and len(id_sequence) == len(animations) and id_sequence != list(range(1, len(id_sequence) + 1)):
        anim_ids = set(id_sequence)
        detail = []
        missing = [i for i in range(1, len(animations) + 1) if i not in anim_ids]
        extra = sorted(anim_ids - set(range(1, len(animations) + 1)))
        if missing:
            detail.append(f"missing {missing}")
        if extra:
            detail.append(f"unexpected {extra}")
        if id_sequence[0] != 1:
            detail.append("first id is not 1")
        err("<animations>", f"ids must form 1..{len(animations)} in document order (the generated array is indexed by id - 1); " + "; ".join(detail))

    anim_ids = set(id_sequence)
    for a in animations:
        for section, transitions in a["transitions"].items():
            for _prob, _only, target, _nwhere in transitions:
                if target is not None and target not in anim_ids:
                    err(f"<animation id={a['id']}> {section}", f"transition target {target} does not reference an existing animation id")
    for where, transitions in spawn_transitions:
        for _prob, _only, target, _nwhere in transitions:
            if target is not None and target not in anim_ids:
                err(f"{where} spawn next", f"transition target {target} does not reference an existing animation id")
    for where, parent, child_target in child_refs:
        if parent is not None and parent not in anim_ids:
            err(where, f"parent animationid {parent} does not reference an existing animation id")
        if child_target is not None and child_target not in anim_ids:
            err(where, f"child <next> target {child_target} does not reference an existing animation id")

    return errors


# ---------------------------------------------------------------- fixtures

# A small, fully valid document.  Fixtures are one-line mutations of it, so
# every failure check below is proven to fire on data other than the
# shipped XML.
BASE_XML = """<?xml version="1.0"?>
<animations xmlns="https://esheep.petrucci.ch/">
<header><tilesx>4</tilesx><tilesy>4</tilesy></header>
<spawns>
<spawn id="1" probability="100">
<x>screenW+10</x>
<y>areaH-imageH</y>
<next probability="100">1</next>
</spawn>
</spawns>
<animations>
<animation id="1">
<name>walk</name>
<start><x>-1</x><y>0</y><interval>100</interval></start>
<end><x>-1</x><y>0</y><interval>100</interval></end>
<sequence repeat="2" repeatfrom="0">
<frame>1</frame>
<frame>2</frame>
<next probability="50" only="none">1</next>
<next probability="50" only="none">2</next>
</sequence>
</animation>
<animation id="2">
<name>idle</name>
<start><x>0</x><y>0</y><interval>100</interval></start>
<end><x>0</x><y>0</y><interval>100</interval></end>
<sequence repeat="0" repeatfrom="0">
<frame>3</frame>
<next probability="100" only="none">1</next>
</sequence>
</animation>
</animations>
<childs>
<child animationid="1">
<x>-imageW-8</x>
<y>imageY</y>
<next>2</next>
</child>
</childs>
</animations>
"""


def _fixture(old, new):
    assert old in BASE_XML, f"fixture anchor not found: {old!r}"
    return BASE_XML.replace(old, new, 1)


def _expect_error(xml, marker):
    errors = validate_xml_text(xml, "<fixture>")
    assert errors, "expected validation errors but none were reported"
    assert any(marker in e for e in errors), (
        f"expected an error containing {marker!r}; got: {errors}"
    )


# ------------------------------------------------------------------- tests

def test_authored_xml_is_valid():
    xml = (REPO_ROOT / "tools" / "esheep_animations.xml").read_text()
    errors = validate_xml_text(xml, "tools/esheep_animations.xml")
    assert errors == [], "authored animation XML must pass validation: " + " | ".join(errors)


def test_base_fixture_is_valid():
    errors = validate_xml_text(BASE_XML, "<base-fixture>")
    assert errors == [], "base fixture must be valid: " + " | ".join(errors)


def test_generation_is_reproducible():
    with tempfile.TemporaryDirectory(prefix="esheep-animation-data-") as tmp:
        out = Path(tmp)
        proc = subprocess.run(
            [sys.executable, "tools/gen_animations.py",
             "tools/esheep_animations.xml", str(out)],
            cwd=REPO_ROOT, capture_output=True, text=True)
        assert proc.returncode == 0, f"generator failed: {proc.stderr}"
        for name in ("animations_data.c", "animations_data.h"):
            got = (out / name).read_bytes()
            want = (REPO_ROOT / "src" / name).read_bytes()
            assert got == want, f"{name} differs from the checked-in generated file"


def test_malformed_animation_id():
    _expect_error(_fixture('<animation id="2">', '<animation id="2b">'), "is not an integer")


def test_duplicate_animation_id():
    _expect_error(_fixture('<animation id="2">', '<animation id="1">'), "duplicate animation id 1")


def test_noncontiguous_animation_ids():
    _expect_error(_fixture('<animation id="2">', '<animation id="3">'), "ids must form 1..2 in document order")


def test_frame_out_of_range():
    _expect_error(_fixture("<frame>1</frame>", "<frame>17</frame>"), "outside tile range 0..15")


def test_frame_not_integer():
    _expect_error(_fixture("<frame>1</frame>", "<frame>1a</frame>"), "<frame> #1 value '1a' is not an integer")


def test_invalid_transition_target():
    _expect_error(_fixture('<next probability="100" only="none">1</next>',
                           '<next probability="100" only="none">99</next>'),
                 "transition target 99 does not reference an existing animation id")


def test_unreachable_transition_bucket():
    xml = _fixture('<next probability="100" only="none">1</next>',
                  '<next probability="100" only="none">1</next>\n<next probability="50" only="none">2</next>')
    _expect_error(xml, "unreachable in context 'none'")


def test_probability_out_of_range():
    _expect_error(_fixture('probability="100" only="none"', 'probability="150" only="none"'),
                 "probability 150 out of range 1..100")


def test_probability_zero():
    _expect_error(_fixture('probability="100" only="none"', 'probability="0" only="none"'),
                 "probability 0 can never fire")


def test_probability_not_integer():
    _expect_error(_fixture('probability="100" only="none"', 'probability="lots" only="none"'),
                 "probability 'lots' is not an integer")


def test_unknown_only_context():
    _expect_error(_fixture('only="none">1</next>\n<next probability="50" only="none">2</next>',
                           'only="diagonal">1</next>\n<next probability="50" only="none">2</next>'),
                 "only='diagonal' is never passed as a runtime context")


def test_unsupported_repeat_expression():
    for bad in ("random/0+5", "spin/2"):
        _expect_error(_fixture('repeat="2"', f'repeat="{bad}"'),
                      "is not a supported repeat expression")


def test_unsupported_action():
    _expect_error(_fixture("<frame>1</frame>",
                           "<frame>1</frame><action>teleport</action>"),
                  "action 'teleport' is not supported")


def test_unsupported_spawn_expression():
    _expect_error(_fixture("<x>screenW+10</x>", "<x>screenW+42</x>"),
                 "is not supported by eval_spawn_expression")


def test_unsupported_child_expression():
    _expect_error(_fixture("<y>imageY</y>", "<y>imageY+4</y>"),
                 "is not supported by eval_child_expression")


def test_invalid_child_parent():
    _expect_error(_fixture('animationid="1"', 'animationid="9"'),
                 "parent animationid 9 does not reference an existing animation id")


def test_invalid_child_target():
    _expect_error(_fixture("<next>2</next>", "<next>99</next>"),
                 "child <next> target 99 does not reference an existing animation id")


def test_empty_sequence():
    _expect_error(_fixture("<frame>3</frame>\n", ""), "sequence has no <frame> elements")

def test_missing_sequence():
    import re as _re
    xml = _re.sub(r"<sequence[^>]*>.*?</sequence>", "", BASE_XML,
                  flags=_re.DOTALL, count=1)
    errors = validate_xml_text(xml, "<fixture>")
    assert errors, str(errors)
    assert any("missing <sequence>" in e for e in errors), errors


def test_noninteger_transition_target():
    _expect_error(_fixture('<next probability="50" only="none">1</next>',
                           '<next probability="50" only="none">1a</next>'),
                  "is not an integer")


def test_noninteger_child_next_target():
    _expect_error(_fixture("<next>2</next>", "<next>2a</next>"),
                  "is not an integer")


def test_noninteger_spawn_next_target():
    _expect_error(_fixture('<next probability="100">1</next>',
                           '<next probability="100">1a</next>'),
                  "is not an integer")


def test_noninteger_header_tilesx():
    _expect_error(_fixture("<tilesx>4</tilesx>", "<tilesx>four</tilesx>"),
                  "is not an integer")


def test_missing_header_tiles():
    xml = (BASE_XML
           .replace("<tilesx>4</tilesx>", "<tilesx>nope</tilesx>", 1)
           .replace("<tilesy>4</tilesy>", "<tilesy>also</tilesy>", 1))
    errors = validate_xml_text(xml, "<fixture>")
    assert errors, str(errors)
    assert any("integer" in e for e in errors), errors


def test_noninteger_interval():
    _expect_error(_fixture("<interval>100</interval>",
                           "<interval>slow</interval>"),
                  "is not a int")


def test_noninteger_offsety():
    xml = _fixture("<start><x>-1</x><y>0</y><interval>100</interval></start>",
                   "<start><x>-1</x><y>0</y><interval>100</interval><offsety>-2.5</offsety></start>")
    _expect_error(xml, "is not an integer (atoi at runtime)")


def test_nonfloat_opacity():
    xml = _fixture("<start><x>-1</x><y>0</y><interval>100</interval></start>",
                   "<start><x>-1</x><y>0</y><interval>100</interval><opacity>full</opacity></start>")
    _expect_error(xml, "is not a float")


def test_nonfinite_opacity():
    xml = _fixture("<start><x>-1</x><y>0</y><interval>100</interval></start>",
                   "<start><x>-1</x><y>0</y><interval>100</interval><opacity>nan</opacity></start>")
    _expect_error(xml, "must be finite")




TESTS = [
    test_original_animation_inventory,
    test_original_child_inventory,
    test_art_completeness_extensions,
    test_authored_xml_is_valid,
    test_base_fixture_is_valid,
    test_generation_is_reproducible,
    test_malformed_animation_id,
    test_duplicate_animation_id,
    test_noncontiguous_animation_ids,
    test_frame_out_of_range,
    test_frame_not_integer,
    test_invalid_transition_target,
    test_unreachable_transition_bucket,
    test_probability_out_of_range,
    test_probability_zero,
    test_probability_not_integer,
    test_unknown_only_context,
    test_unsupported_repeat_expression,
    test_unsupported_action,
    test_unsupported_spawn_expression,
    test_unsupported_child_expression,
    test_invalid_child_parent,
    test_invalid_child_target,
    test_empty_sequence,
    test_missing_sequence,
    test_noninteger_transition_target,
    test_noninteger_child_next_target,
    test_noninteger_spawn_next_target,
    test_noninteger_header_tilesx,
    test_missing_header_tiles,
    test_noninteger_interval,
    test_noninteger_offsety,
    test_nonfloat_opacity,
    test_nonfinite_opacity,
]


if __name__ == "__main__":
    failures = 0
    for fn in TESTS:
        try:
            fn()
            print(f"OK: {fn.__name__}")
        except AssertionError as exc:
            failures += 1
            print(f"FAIL: {fn.__name__}: {exc}", file=sys.stderr)
        except Exception as exc:  # a validator crash is a test failure
            failures += 1
            print(f"ERROR: {fn.__name__}: {exc!r}", file=sys.stderr)
    if failures:
        print(f"\n{failures} animation data validation test(s) failed.", file=sys.stderr)
        sys.exit(1)
    print(f"\nAll {len(TESTS)} animation data validation tests passed!")
    sys.exit(0)
