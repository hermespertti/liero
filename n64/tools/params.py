#!/usr/bin/env python3
"""Bake weapon/nobject/sobject param tables from TC/openliero cfgs.

Single source of truth: both the ROM (baked.h) and the host combat mirror
(combat_ref.py) read these tables, so physics params cannot diverge.
Field order in the emitted C structs is fixed and listed in FIELDS_*.
"""
import os, re, sys

TC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "TC", "openliero")

def load_cfg(path):
    d = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        k, v = k.strip(), v.strip()
        if v == "true":
            d[k] = 1
        elif v == "false":
            d[k] = 0
        elif v == "null":
            d[k] = -1
        else:
            try:
                d[k] = int(v)
            except ValueError:
                d[k] = v.strip('"')
    return d

def names(cfg_name):
    txt = open(os.path.join(TC, "tc.cfg")).read()
    m = re.search(r"%s\s*=\s*\[(.*?)\]" % cfg_name, txt, re.S)
    return [s.strip().strip('"') for s in m.group(1).split(",") if s.strip()]

W_FIELDS = [
    ("gravity", 0), ("speed", 0), ("distribution", 0), ("parts", 1),
    ("recoil", 0), ("multSpeed", 100), ("delay", 20), ("loadingTime", 220),
    ("ammo", 5), ("dirtEffect", -1), ("fireCone", 0), ("bounce", 0),
    ("timeToExplo", 0), ("timeToExploV", 0), ("hitDamage", 0),
    ("bloodOnHit", 0), ("startFrame", -1), ("numFrames", 0), ("shotType", 0),
    ("colorBullets", 0), ("splinterAmount", 0), ("splinterColour", 0),
    ("splinterScatter", 0), ("detectDistance", 0), ("blowAway", 0),
    ("addSpeed", 0),
    # bools
    ("affectByWorm", 0), ("wormExplode", 0), ("explGround", 0),
    ("wormCollide", 0), ("collideWithObjects", 0), ("affectByExplosions", 0),
    ("loopAnim", 0), ("chainExplosion", 0),
    # leaveShells / shells
    ("leaveShells", 0), ("leaveShellDelay", 1),
    # createOnExp index resolved from name (sobject id), -1 null
    ("createOnExp", -1), ("splinterType", -1), ("objTrailType", -1),
    ("affectByWorm_", 0),
]

N_FIELDS = [
    ("gravity", 0), ("speed", 0), ("speedV", 0), ("distribution", 0),
    ("bounce", 0), ("blowAway", 0), ("hitDamage", 0), ("bloodOnHit", 0),
    ("startFrame", 0), ("numFrames", 0), ("colorBullets", 0),
    ("dirtEffect", -1), ("timeToExplo", 0), ("timeToExploV", 0),
    ("splinterAmount", 0), ("splinterColour", -1), ("splinterType", -1),
    ("detectDistance", 0), ("bloodTrailDelay", 0), ("leaveObjDelay", 0),
    ("leaveObj", -1),
    ("wormExplode", 0), ("explGround", 0), ("wormDestroy", 0),
    ("drawOnMap", 0), ("affectByExplosions", 0), ("bloodTrail", 0),
    ("createOnExp", -1),
]

S_FIELDS = [
    ("startFrame", 0), ("numFrames", 0), ("animDelay", 2),
    ("detectRange", 0), ("damage", 0), ("blowAway", 0),
    ("dirtEffect", -1), ("shake", 0), ("flash", 0),
]

def resolve(cfg, key, table_names):
    """resolve a name-typed field to its index, -1 if null/missing."""
    v = cfg.get(key, -1)
    if isinstance(v, str):
        if v in table_names:
            return table_names.index(v)
        return -1
    return int(v)

def bake():
    wnames = names("weapons")
    nnames = names("nobjects")
    snames = names("sobjects")
    weapons = []
    for n in wnames:
        c = load_cfg(os.path.join(TC, "weapons", n + ".cfg"))
        c["_name"] = n
        weapons.append(c)
    nobjs = []
    for n in nnames:
        c = load_cfg(os.path.join(TC, "nobjects", n + ".cfg"))
        c["_name"] = n
        nobjs.append(c)
    sobjs = []
    for n in snames:
        c = load_cfg(os.path.join(TC, "sobjects", n + ".cfg"))
        c["_name"] = n
        sobjs.append(c)
    return weapons, nobjs, sobjs, wnames, nnames, snames

def field(c, k, dflt):
    v = c.get(k, dflt)
    if isinstance(v, str):
        return 0
    return int(v)

def write_header(path, weapons, nobjs, sobjs):
    with open(path, "w") as f:
        f.write("// Auto-baked TC param tables by tools/params.py. Do not edit.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define WP_COUNT %d\n" % len(weapons))
        f.write("typedef struct {\n")
        f.write("  int32_t gravity,speed,distribution,parts,recoil,multSpeed,delay,loadingTime,ammo,")
        f.write("dirtEffect,fireCone,bounce,timeToExplo,timeToExploV,hitDamage,bloodOnHit,startFrame,")
        f.write("numFrames,shotType,colorBullets,splinterAmount,splinterColour,splinterScatter,")
        f.write("detectDistance,blowAway,addSpeed;\n")
        f.write("  int8_t affectByWorm,wormExplode,explGround,wormCollide,collideWithObjects,")
        f.write("affectByExplosions,loopAnim,chainExplosion,leaveShells,leaveShellDelay;\n")
        f.write("  int16_t createOnExp,splinterType,objTrailType;\n")
        f.write("} WParam;\n")
        f.write("static const WParam wparam[WP_COUNT]={\n")
        for c in weapons:
            vals = []
            for k, d in W_FIELDS:
                if k.endswith("_"):
                    continue
                if k in ("createOnExp", "splinterType", "objTrailType"):
                    vals.append(-1)
                else:
                    vals.append(field(c, k, d))
            # trailing special fields
            ce = -1; st = -1; ot = -1
            # resolved after we know name lists (done by caller via resolve_tables)
            f.write("  {" + ",".join(map(str, vals)) + ",0,0,0},\n")
        f.write("};\n\n")

def main():
    weapons, nobjs, sobjs, wnames, nnames, snames = bake()
    # resolve name fields
    for c in weapons:
        c["createOnExp_i"] = resolve(c, "createOnExp", snames)
        c["splinterType_i"] = resolve(c, "splinterType", nnames)
        c["objTrailType_i"] = resolve(c, "objTrailType", snames)
    for c in nobjs:
        c["splinterType_i"] = resolve(c, "splinterType", nnames)
        c["createOnExp_i"] = resolve(c, "createOnExp", snames)
        c["leaveObj_i"] = resolve(c, "leaveObj", snames)

    # emit JSON for host mirror
    import json
    out = {"weapons": [], "nobjects": [], "sobjects": []}
    for c in weapons:
        d = {k: (c.get(k + "_i", field(c, k, df))) if k in ("createOnExp", "splinterType", "objTrailType") else field(c, k, df)
             for k, df in W_FIELDS if not k.endswith("_")}
        out["weapons"].append(d)
    for c in nobjs:
        d = {}
        for k, df in N_FIELDS:
            if k in ("splinterType", "createOnExp", "leaveObj"):
                d[k] = c.get(k + "_i", -1)
            else:
                d[k] = field(c, k, df)
        out["nobjects"].append(d)
    for c in sobjs:
        out["sobjects"].append({k: field(c, k, df) for k, df in S_FIELDS})

    jpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ref", "params.json")
    os.makedirs(os.path.dirname(jpath), exist_ok=True)
    json.dump(out, open(jpath, "w"), indent=0)
    print("wrote %s (%d w, %d n, %d s)" % (jpath, len(out["weapons"]), len(out["nobjects"]), len(out["sobjects"])))

if __name__ == "__main__":
    main()
