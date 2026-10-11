// Duel: AI control trace + deaths + checkpoints at full speed (no rendering).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
if (!ares.waitLog("GEN ", 60)) { console.log("GEN TIMEOUT"); ares.exit(1); }
if (!ares.waitLog("CK frame=1259", 600)) { console.log("CK1259 TIMEOUT"); }
var lg = ares.log().split("\n");
var n = 0;
for (var i = 0; i < lg.length; i++) {
  var L = lg[i];
  if (L.indexOf("CK ") === 0 || L.indexOf("CW ") === 0 || L.indexOf("SPAWN") === 0 ||
      L.indexOf("AI ") === 0 || L.indexOf("A0 ") === 0 ||
      L.indexOf("WT ") === 0 || L.indexOf("WC ") === 0 || L.indexOf("WS ") === 0 || L.indexOf("FS ") === 0 || L.indexOf("DS ") === 0 || L.indexOf("RR ") === 0 || L.indexOf("FW ") === 0 || L.indexOf("AS ") === 0 || L.indexOf("NT ") === 0 || L.indexOf("ST ") === 0 ||
      L.indexOf("BO ") === 0 || L.indexOf("NN ") === 0 ||
      L.indexOf("DMG ") === 0 || L.indexOf("DIE ") === 0 || L.indexOf("RSP ") === 0 ||
      L.indexOf("OVR ") === 0 || L.indexOf("RG ") === 0) {
    console.log(L); n++;
  }
}
console.log("CKLINES " + n);
ares.exit(0);
