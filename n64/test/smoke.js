// Game ROM visual smoke: renderer on, capture at several wall-clock points.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
var outdir = "/home/lex/n64/liero/n64/shots";
for (var i = 0; i < 3; i++) {
    ares.waitFrames(180);
    try {
        var s = ares.screenshot();
        s.save(outdir + "/smoke_s5_" + (i*6) + ".png");
        console.log("captured " + i);
    } catch (e) { console.log("cap fail " + i + ": " + e); }
}
ares.exit(0);
