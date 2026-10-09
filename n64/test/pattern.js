// Capture multiple frames; black mid-transfer shots are discarded host-side.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(40);
for (var i = 0; i < 8; i++) {
    try {
        var s = ares.screenshot();
        s.save("/home/lex/n64/liero/n64/shots/pattern_" + i + ".png");
    } catch (e) {}
    ares.waitFrames(7);
}
console.log("done");
ares.exit(0);
