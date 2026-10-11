#!/usr/bin/env python3
"""Bake combat param tables from TC/openliero cfgs -> ref/combat_params.json + assets/combat.h

Field order is FIXED and shared between ROM C and the host mirror. Name-typed
fields (createOnExp, splinterType, objTrailType, leaveObj) are resolved to
list indices. Do not reorder without updating both consumers.
"""
import os, re, json

HERE = os.path.dirname(os.path.abspath(__file__))
TC = os.path.normpath(os.path.join(HERE, "..", "..", "TC", "openliero"))

def load_cfg(path):
    d = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip()
        if v == "true":    d[k] = 1
        elif v == "false": d[k] = 0
        elif v == "null":  d[k] = -1
        else:
            try:    d[k] = int(v)
            except ValueError: d[k] = v.strip('"')
    return d

def names(key):
    txt = open(os.path.join(TC, "tc.cfg")).read()
    m = re.search(r"%s\s*=\s*\[(.*?)\]" % key, txt, re.S)
    return [s.strip().strip('"') for s in m.group(1).split(",") if s.strip()]

# weapon params (order = WK_* enum)
WEAPONS_KEYS = [
    "gravity","speed","distribution","parts","recoil","multSpeed","delay","loadingTime",
    "ammo","dirtEffect","fireCone","bounce","timeToExplo","timeToExploV","hitDamage",
    "bloodOnHit","startFrame","numFrames","shotType","colorBullets","splinterAmount",
    "splinterColour","splinterType","splinterScatter","detectDistance","blowAway","addSpeed",
    "affectByWorm","wormExplode","explGround","wormCollide","collideWithObjects",
    "affectByExplosions","loopAnim","chainExplosion","leaveShells","leaveShellDelay",
    "objTrailType","objTrailDelay","partTrailType","partTrailObj","partTrailDelay","createOnExp",
]
NOBJ_KEYS = [
    "gravity","speed","speedV","distribution","bounce","blowAway","hitDamage","bloodOnHit",
    "startFrame","numFrames","colorBullets","dirtEffect","timeToExplo","timeToExploV",
    "splinterAmount","splinterColour","splinterType","detectDistance","bloodTrailDelay",
    "leaveObjDelay","leaveObj","wormExplode","explGround","wormDestroy","drawOnMap",
    "affectByExplosions","bloodTrail","createOnExp",
]
SOBJ_KEYS = ["startFrame","numFrames","animDelay","detectRange","damage","blowAway","dirtEffect","shake","flash","startSound","numSounds"]

NAME_FIELDS = {"createOnExp": "sobjects", "splinterType": "nobjects",
               "objTrailType": "sobjects", "leaveObj": "sobjects"}

def resolve(c, k, nlists):
    v = c.get(k, -1)
    if isinstance(v, str):
        lst = nlists[NAME_FIELDS[k]]
        return lst.index(v) if v in lst else -1
    return int(v)

def row(c, keys, nlists):
    out = []
    for k in keys:
        if k in NAME_FIELDS:
            out.append(resolve(c, k, nlists))
        else:
            v = c.get(k, 0)
            out.append(int(v) if not isinstance(v, str) else 0)
    return out

def main():
    nlists = {"weapons": names("weapons"), "nobjects": names("nobjects"), "sobjects": names("sobjects")}
    out = {"w": [], "n": [], "s": [], "idx": {}, "const": {}}

    out["idx"]["blood"]    = nlists["nobjects"].index("blood")
    out["idx"]["shells"]   = nlists["nobjects"].index("shells")
    out["idx"]["part2"]    = nlists["nobjects"].index("particle__disappearing")
    out["idx"]["smallexp"] = nlists["sobjects"].index("small_explosion")
    out["idx"]["dirt"]     = nlists["nobjects"].index("dirt")
    out["idx"]["dirtball"] = nlists["weapons"].index("dirtball")
    out["idx"]["handgun"]  = nlists["weapons"].index("handgun")

    for n in nlists["weapons"]:
        out["w"].append(row(load_cfg(os.path.join(TC, "weapons", n + ".cfg")), WEAPONS_KEYS, nlists))
    for n in nlists["nobjects"]:
        out["n"].append(row(load_cfg(os.path.join(TC, "nobjects", n + ".cfg")), NOBJ_KEYS, nlists))
    for n in nlists["sobjects"]:
        out["s"].append(row(load_cfg(os.path.join(TC, "sobjects", n + ".cfg")), SOBJ_KEYS, nlists))

    # section-aware parse of [constants.aiparams.<name>] on/off pairs
    # model: ar.obj(name){ "on" -> k[1][i], "off" -> k[0][i] }, i in ctrl order
    AI_ORDER = ["up","down","left","right","fire","change","jump"]
    txt = open(os.path.join(TC, "tc.cfg")).read()
    k2 = [[0]*7, [0]*7]
    for i, nm in enumerate(AI_ORDER):
        m = re.search(r"\[constants\.aiparams\.%s\]\s*\n\s*on\s*=\s*(\d+)\s*\n\s*off\s*=\s*(\d+)" % nm, txt)
        assert m, "aiparams %s not found" % nm
        k2[1][i] = int(m.group(1))
        k2[0][i] = int(m.group(2))
    out["ai_k"] = k2

    tc = load_cfg(os.path.join(TC, "tc.cfg"))
    for k in ("WormMinSpawnDistLast","WormMinSpawnDistEnemy","WormSpawnRectX","WormSpawnRectY",
              "WormSpawnRectW","WormSpawnRectH","FirstBloodColour","NumBloodColours","BObjGravity",
              "BonusDropChance","BonusGravity","BonusBounceMul","BonusBounceDiv",
              "BonusSpawnRectW","BonusSpawnRectH"):
        out["const"][k] = int(tc.get(k, 0))

    j = os.path.normpath(os.path.join(HERE, "..", "ref", "combat_params.json"))
    os.makedirs(os.path.dirname(j), exist_ok=True)
    json.dump(out, open(j, "w"), separators=(",", ":"))
    print("wrote %s (%d w/%d n/%d s)" % (j, len(out["w"]), len(out["n"]), len(out["s"])))

    h = os.path.normpath(os.path.join(HERE, "..", "assets", "combat.h"))
    with open(h, "w") as f:
        f.write("// Auto-baked combat params by tools/params2.py. Do not edit.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define CW_N %d\nstatic const int32_t c_wparam[CW_N][%d]={\n" % (len(out["w"]), len(WEAPONS_KEYS)))
        for r in out["w"]: f.write("  {" + ",".join(map(str, r)) + "},\n")
        f.write("};\n\n")
        f.write("#define CN_N %d\nstatic const int32_t c_nparam[CN_N][%d]={\n" % (len(out["n"]), len(NOBJ_KEYS)))
        for r in out["n"]: f.write("  {" + ",".join(map(str, r)) + "},\n")
        f.write("};\n\n")
        f.write("#define CS_N %d\nstatic const int32_t c_sparam[CS_N][%d]={\n" % (len(out["s"]), len(SOBJ_KEYS)))
        for r in out["s"]: f.write("  {" + ",".join(map(str, r)) + "},\n")
        f.write("};\n\n")
        i = out["idx"]
        f.write("enum { IDX_BLOOD=%d, IDX_SHELLS=%d, IDX_PART2=%d, IDX_SMALLEXPC=%d, IDX_DIRT=%d, IDX_DIRTBALL=%d, IDX_HANDGUN=%d };\n\n" % (
            i["blood"], i["shells"], i["part2"], i["smallexp"], i["dirt"], i["dirtball"], i["handgun"]))
        c = out["const"]
        f.write("enum { C_SPAWN_X=%d, C_SPAWN_Y=%d, C_SPAWN_W=%d, C_SPAWN_H=%d, C_SPAWN_LAST=%d, C_SPAWN_ENEMY=%d };\n" % (
            c["WormSpawnRectX"], c["WormSpawnRectY"], c["WormSpawnRectW"], c["WormSpawnRectH"],
            c["WormMinSpawnDistLast"], c["WormMinSpawnDistEnemy"]))
        f.write("enum { C_FIRSTBLOOD=%d, C_NUMBLOOD=%d, C_BOBJGRAV=%d, C_BONUSDROP=%d, C_BONUSGRAV=%d, C_BONUSBOUNCE=%d };\n\n" % (
            c["FirstBloodColour"], c["NumBloodColours"], c["BObjGravity"], c["BonusDropChance"],
            c["BonusGravity"], c["BonusBounceMul"]))
        f.write("enum WKEY { %s };\n" % ", ".join("WK_%s=%d" % (k, n) for n, k in enumerate(WEAPONS_KEYS)))
        f.write("enum NKEY { %s };\n" % ", ".join("NK_%s=%d" % (k, n) for n, k in enumerate(NOBJ_KEYS)))
        f.write("enum SKEY { %s };\n" % ", ".join("SK_%s=%d" % (k, n) for n, k in enumerate(SOBJ_KEYS)))
        f.write("\n// AI params: k[2][7] [state(pressed)][ctrl]; ctrl order Up,Down,Left,Right,Fire,Change,Jump\n")
        f.write("static const int32_t c_ai_k[2][7]={\n")
        for r in out["ai_k"]: f.write("  {" + ",".join(map(str, r)) + "},\n")
        f.write("};\n")
        tcfg = load_cfg(os.path.join(TC, "tc.cfg"))
        f.write("enum { C_WORMFLOAT=%d, C_WORMFLOATLEVEL=%d, C_WORMFLOATPOWER=%d };\n" % (
            int(bool(tcfg.get("WormFloat", 0))), int(tcfg.get("WormFloatLevel", 0)), int(tcfg.get("WormFloatPower", 0))))
    print("wrote %s" % h)

if __name__ == "__main__":
    main()
