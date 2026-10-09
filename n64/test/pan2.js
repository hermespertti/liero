// Capture terrain at 3 camera holds with generous spacing to avoid capture desync.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
if (ares.waitLog("GEN ", 60)) {
    var m = ares.log().match(/GEN hash=([0-9a-f]+)/);
    console.log("ROMGEN " + (m ? m[1] : "PARSE_FAIL"));
} else {
    console.log("ROMGEN TIMEOUT");
}
var outdir = "/home/lex/n64/liero/n64/shots";
// schedule pos = frame/200 % 8 -> camx = pos*26; capture mid-hold
var marks = [[100, 0], [300, 26], [500, 52], [700, 78], [900, 104]];
var prev = 0;
for (var i = 0; i < marks.length; i++) {
    var n = marks[i][0] - prev; prev = marks[i][0];
    ares.waitFrames(n);
    try {
        var s = ares.screenshot();
        s.save(outdir + "/c2_" + marks[i][1] + ".png");
        console.log("saved c2_" + marks[i][1]);
    } catch (e) { console.log("shot err " + e); }
}
ares.exit(0);
