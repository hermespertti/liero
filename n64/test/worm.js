// Worm slice smoke: capture F trace lines + screenshots of worm walking/jumping.
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
if (!ares.waitLog("GEN ", 60)) { console.log("GEN TIMEOUT"); ares.exit(1); }
var m = ares.log().match(/GEN hash=([0-9a-f]+)/);
console.log("ROMGEN " + (m ? m[1] : "PARSE_FAIL"));
var marks = [90, 210, 330, 420];
var prev = 0;
for (var i = 0; i < marks.length; i++) {
    ares.waitFrames(marks[i] - prev); prev = marks[i];
    try {
        var s = ares.screenshot();
        s.save("/home/lex/n64/liero/n64/shots/worm_" + marks[i] + ".png");
        console.log("saved worm_" + marks[i]);
    } catch (e) { console.log("shot err " + e); }
}
// dump full log
var lg = ares.log().split("\n");
var out = [];
for (var i = 0; i < lg.length; i++) if (lg[i].indexOf("F frame=") >= 0) out.push(lg[i]);
console.log("TRACE_BEGIN");
for (var i = 0; i < out.length; i++) console.log(out[i]);
console.log("TRACE_END");
ares.exit(0);
