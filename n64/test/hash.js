// Fast ISViewer hash check (renderer none => full speed).
ares.setHomebrew(true);
ares.setRenderer("none");
ares.loadRom(ares.args[0]);
ares.resume();

if (ares.waitLog("GEN ", 30)) {
    var m = ares.log().match(/GEN hash=([0-9a-f]+)/);
    console.log("ROMGEN " + (m ? m[1] : "PARSE_FAIL"));
} else {
    console.log("ROMGEN TIMEOUT");
}
ares.exit(0);
