// Weapons visual capture: waitFrames marks (pan2-proven), burst shots, discard-black host-side.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
if (ares.waitLog("GEN ", 120)) {
    var m = ares.log().match(/GEN hash=([0-9a-f]+)/);
    console.log("ROMGEN " + (m ? m[1] : "PARSE_FAIL"));
} else {
    console.log("ROMGEN TIMEOUT");
}
var outdir = "/home/lex/n64/liero/n64/shots";
var marks = [112, 137, 162, 180];
var prev = 0;
for (var i = 0; i < marks.length; i++) {
    var n = marks[i] - prev; prev = marks[i];
    ares.waitFrames(n);
    for (var b = 0; b < 3; b++) {
        try {
            var s = ares.screenshot();
            s.save(outdir + "/w" + marks[i] + "_" + b + ".png");
        } catch (e) { console.log("shot err " + e); }
    }
    console.log("burst " + marks[i]);
}
ares.exit(0);
