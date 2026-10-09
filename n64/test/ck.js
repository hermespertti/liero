// Weapons checkpoint trace at full speed (no rendering).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
if (!ares.waitLog("GEN ", 60)) { console.log("GEN TIMEOUT"); ares.exit(1); }
if (!ares.waitLog("CK frame=839", 240)) { console.log("CK839 TIMEOUT"); }
var lg = ares.log().split("\n");
var n = 0;
for (var i = 0; i < lg.length; i++) {
  if (lg[i].indexOf("CK ") === 0 || lg[i].indexOf("SPAWN") === 0 || lg[i].indexOf("WT ") === 0) { console.log(lg[i]); n++; }
}
console.log("CKLINES " + n);
ares.exit(0);
