// liero64 slice-1: verify GEN hash via ISViewer + capture parity shots.
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
// AUTOTEST camx schedule: pos = frame/200 % 8, camx = pos*26
var marks = [[250, 26], [450, 52], [850, 104]];
var prev = 0;
for (var i = 0; i < marks.length; i++) {
    var n = marks[i][0] - prev; prev = marks[i][0];
    ares.waitFrames(n);
    try {
        var s = ares.screenshot();
        s.save(outdir + "/cam_" + marks[i][1] + ".png");
        console.log("saved cam_" + marks[i][1] + ".png");
    } catch (e) {
        console.log("shot " + marks[i][0] + " failed: " + e);
    }
}
console.log("done");
