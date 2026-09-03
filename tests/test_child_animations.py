#!/usr/bin/env python3
"""Test child animation data generation and consistency."""
import sys
import re
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"e": "https://esheep.petrucci.ch/"}

def tag(el, name, default=None, cast=str):
    if el is None:
        return default
    child = el.find(f"e:{name}", NS)
    if child is None or child.text is None:
        return default
    return cast(child.text.strip())

def test_child_records_valid():
    """Verify all child animation records reference valid animations."""
    xml_path = Path("tools/esheep_animations.xml")
    root = ET.fromstring(xml_path.read_text())

    # Get all animation IDs
    animation_ids = set()
    for an in root.findall("e:animations/e:animation", NS):
        animation_ids.add(int(an.get("id")))

    # Validate child records
    childs_el = root.find("e:childs", NS)
    if childs_el is None:
        print("OK: No child records found")
        return True

    child_count = 0
    for child in childs_el.findall("e:child", NS):
        anim_id = int(child.get("animationid"))
        target_id = tag(child, "next", "0", int)

        # Parent animation must exist
        assert anim_id in animation_ids, \
            f"Child parent animation {anim_id} not found"

        # Target animation must exist
        assert target_id in animation_ids, \
            f"Child target animation {target_id} not found"

        # Verify offset expressions are not empty
        x_expr = tag(child, "x", "", str)
        y_expr = tag(child, "y", "", str)
        assert x_expr, f"Child animation {anim_id} has empty x expression"
        assert y_expr, f"Child animation {anim_id} has empty y expression"

        child_count += 1

    print(f"OK: {child_count} child animation records validated")
    return True

def test_eating_animation_has_flower():
    """Verify eating animation (26) has flower child (27)."""
    xml_path = Path("tools/esheep_animations.xml")
    root = ET.fromstring(xml_path.read_text())

    childs_el = root.find("e:childs", NS)
    assert childs_el is not None, "No childs element found"

    eating_child = None
    for child in childs_el.findall("e:child", NS):
        if int(child.get("animationid")) == 26:
            eating_child = child
            break

    assert eating_child is not None, "Eating animation (26) has no child record"
    target = tag(eating_child, "next", "0", int)
    assert target == 27, f"Eating animation child should target 27, got {target}"

    # Verify offset expressions exist
    x_expr = tag(eating_child, "x", "", str)
    y_expr = tag(eating_child, "y", "", str)
    assert x_expr, "Eating child x expression is empty"
    assert y_expr, "Eating child y expression is empty"

    # The first child frame is the authored blank lead-in. Later frames must
    # contain the visible flower growth sequence.
    anim_data = Path("src/animations_data.c").read_text()
    flower = re.search(r"anim27_frames\[\] = \{([^}]*)\};", anim_data)
    assert flower is not None, "Flower animation frames are missing"
    frames = [int(value) for value in re.findall(r"\d+", flower.group(1))]
    assert frames[0] == 174, "Flower must preserve its blank lead-in frame"
    assert any(frame in (149, 150, 151, 152, 153) for frame in frames[1:]), \
        "Flower animation has no visible growth frames"

    print("OK: Eating animation (26) has correct flower child (27)")
    return True

def test_generated_child_data():
    """Verify generated C data includes child records."""
    import re

    anim_data_c = Path("src/animations_data.c").read_text()

    # Check for EsheepChild array
    assert "const EsheepChild esheep_default_childs[]" in anim_data_c, \
        "Generated data missing esheep_childs array"

    # Verify count matches
    count_match = re.search(r"const int esheep_default_child_count = (\d+);", anim_data_c)
    assert count_match, "Generated data missing esheep_child_count"
    child_count = int(count_match.group(1))
    assert child_count == 3, f"Expected 3 child records, got {child_count}"

    # Verify the eating-to-flower mapping is in generated data
    assert re.search(r"26.*\"imageX-imageW\*0\.9\".*\"imageY\".*27", anim_data_c), \
        "Generated data missing eating-to-flower child record"

    print(f"OK: Generated C data has {child_count} child records")
    return True

def test_child_review_uses_parent():
    """The visual review must launch a child transition from its parent."""
    review = Path("tools/review_transitions.py").read_text()
    assert 'transition["kind"] == "child"' in review
    assert '"--review-parent"' in review
    assert 'review_id = source' in review
    print("OK: Child review launches the authored parent scene")
    return True

def test_child_coordinate_semantics():
    """Relative black-sheep placement must stay beside its parent."""
    source = Path("src/main.c").read_text()
    expression = Path("src/expression.c").read_text()
    assert 'image_x' in expression and 'image_width' in expression
    assert 'esheep_expression_eval(expr' in source
    print("OK: Relative child coordinates use the shared expression evaluator")
    return True

if __name__ == "__main__":
    try:
        test_child_records_valid()
        test_eating_animation_has_flower()
        test_generated_child_data()
        test_child_review_uses_parent()
        test_child_coordinate_semantics()
        print("\nAll child animation tests passed!")
        sys.exit(0)
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
