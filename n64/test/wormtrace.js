// Worm physics trace at full speed (no GPU rendering).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();
if (!ares.waitLog("GEN ", 60)) { console.log("GEN TIMEOUT"); ares.exit(1); }
ares.waitFrames(300);
var lg = ares.log().split("\n");
var out = [];
for (var i = 0; i < lg.length; i++) out.push(lg[i]);
console.log("TRACE " + out.length);
for (var i = 0; i < out.length; i++) console.log(out[i]);
ares.exit(0);
