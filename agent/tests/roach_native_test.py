"""Check the 0.9.0n cockroachMaster UAssetAPI JSON used to design admission.

Usage: python roach_native_test.py path/to/unfiltered-roach-full.json
This is an offline bytecode contract, not rendered gameplay or a live engine call.
"""
import json
import sys

with open(sys.argv[1], encoding="utf-8-sig") as f:
    asset = json.load(f)

exports = {entry.get("ObjectName"): entry for entry in asset["Exports"]}
imports = asset["Imports"]


def opname(node):
    return node["$type"].split(",", 1)[0].rsplit(".", 1)[-1]


def contains(node, text):
    return text in json.dumps(node, separators=(",", ":"))


def import_name(index):
    assert index < 0
    return imports[-index - 1]["ObjectName"]


add = exports["addRoach"]
params = {p["Name"]: p["SerializedType"] for p in add["LoadedProperties"]
          if "CPF_Parm" in p["PropertyFlags"]}
assert params == {"location": "StructProperty", "size": "StructProperty",
                  "bypassCheck": "BoolProperty"}
b = add["ScriptBytecode"]
assert import_name(b[0]["AssignmentExpression"]["StackNode"]) == "GetPlayerCameraManager"
assert import_name(b[2]["Expression"]["StackNode"]) == "Vector_Distance"
assert contains(b[2], '"location"')
assert import_name(b[3]["AssignmentExpression"]["StackNode"]) == "Less_FloatFloat"
assert b[3]["AssignmentExpression"]["Parameters"][1]["Value"] == 10000.0
assert opname(b[4]) == "EX_JumpIfNot" and b[4]["CodeOffset"] == 1729
assert contains(b[5], '"count"') and contains(b[5], '"maxAmount"')
assert opname(b[6]) == "EX_JumpIfNot" and b[6]["CodeOffset"] == 1729
assert opname(b[7]) == "EX_JumpIfNot" and contains(b[7], '"bypassCheck"')
assert b[7]["CodeOffset"] == 681
assert import_name(b[11]["AssignmentExpression"]["StackNode"]) == "LessEqual_FloatFloat"
assert b[11]["AssignmentExpression"]["Parameters"][1]["Value"] == 2000.0
assert contains(b[13], '"bannedMats"') and contains(b[14], '"CallFunc_Array_Contains_ReturnValue"')
assert b[16]["CodeOffset"] == 1729
assert opname(b[20]) == "EX_JumpIfNot" and b[20]["CodeOffset"] == 1729
assert b[23]["CodeOffset"] == 236 and b[24]["CodeOffset"] == 1729
assert import_name(b[31]["AssignmentExpression"]["StackNode"]) == "AddComponent"
assert import_name(b[32]["Expression"]["ContextExpression"]["StackNode"]) == "Array_Add"
assert contains(b[32], '"roaches"') and contains(b[32], '"CallFunc_AddComponent_ReturnValue"')
assert contains(b[33], '"location"') and contains(b[36], '"count"')

delete = exports["deleteRoach"]
params = {p["Name"]: p["SerializedType"] for p in delete["LoadedProperties"]
          if "CPF_Parm" in p["PropertyFlags"]}
assert params == {"IndexToRemove": "IntProperty", "crush": "BoolProperty"}
b = delete["ScriptBytecode"]
assert import_name(b[5]["ContextExpression"]["StackNode"]) == "K2_DestroyComponent"
assert import_name(b[6]["ContextExpression"]["StackNode"]) == "Array_Set"
assert contains(b[6], '"roaches"') and contains(b[6], '"IndexToRemove"')
assert contains(b[9], '"count"')

# Ordinary no-hit crawl is along the current component forward axis. Surface
# impacts build a 3-D rotation from the hit normal, which position-only packets
# cannot reproduce exactly and this offline contract does not claim to simulate.
current = exports["updCurrent"]["ScriptBytecode"]
assert import_name(current[2]["Expression"]["ContextExpression"]["StackNode"]) == "GetForwardVector"
assert contains(current[3], '"forward"')
calc = exports["calc"]["ScriptBytecode"]
assert import_name(calc[72]["Expression"]["StackNode"]) == "RandomFloatInRange"
assert [p["Value"] for p in calc[72]["Expression"]["Parameters"]] == [100.0, 150.0]
assert contains(calc[76], '"forward"') and contains(calc[77], '"loc"')
assert import_name(calc[68]["Expression"]["StackNode"]) == "MakeRotationFromAxes"
assert contains(calc[68], '"CallFunc_BreakHitResult_ImpactNormal_1"')
assert import_name(calc[69]["ContextExpression"]["StackNode"]) == "K2_SetWorldRotation"
print("PASS cooked native roach admission, movement and surface-rotation contracts")
