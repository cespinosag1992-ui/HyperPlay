// node --test test/redact.test.js   (sin dependencias)
import { test } from "node:test";
import assert from "node:assert/strict";
import { redact, redactReport } from "../src/redact.js";
import worker, { reportsRepo } from "../src/index.js";

const cases = [
  ["connect to 192.168.1.37:443 failed", "connect to <ip>:443 failed"],
  ["bind 127.0.0.1 0.0.0.0", "bind 127.0.0.1 0.0.0.0"],
  ["lib 1.2.3.4.5", "lib 1.2.3.4.5"],
  ["addr 2001:0db8:85a3:0000:0000:8a2e:0370:7334 up", "addr <ip> up"],
  ["addr fe80::1c2d:3e4f%wlan0", "addr <ip>%wlan0"],
  ["[2001:db8::42]:80", "[<ip>]:80"],
  ["11-26 12:34:56.789 I/x", "11-26 12:34:56.789 I/x"],
  ["std::vector<int> Foo::bar() ::1", "std::vector<int> Foo::bar() ::1"],
  ["Account {name=juan.perez+x@gmail.com, type=com.google}", "Account {name=<email>, type=com.google}"],
  ["Juego: /storage/emulated/0/Juan Pérez/Juegos/Hollow Knight/hollow_knight.exe", "Juego: <storage>/…/Hollow Knight/hollow_knight.exe"],
  ["/sdcard/Download/Celeste/Celeste.exe", "<storage>/…/Celeste/Celeste.exe"],
  ["/storage/1A2B-3C4D/Mis cosas/Ori/ori.exe", "<storage>/…/Ori/ori.exe"],
  ["/storage/emulated/10/Celeste", "<storage>/Celeste"],
  ["C:\\Users\\Murdoc\\AppData\\Roaming", "C:\\Users\\<user>\\AppData\\Roaming"],
  ["/home/carlos/x and /Users/ana/y", "/home/<user>/x and /Users/<user>/y"],
  ["wlan0 a4:5e:60:c1:22:9f ok", "wlan0 <mac> ok"],
  ["eth A4-5E-60-C1-22-9F", "eth <mac>"],
  ["android_id=9774d56d682e549c serial: R5CT12ABCDE", "android_id=<device-id> serial: <device-id>"],
  ["ro.serialno=ZY22H7K9LP", "ro.serialno=<device-id>"],
  ["imei 490154203237518", "imei <device-id>"],
  ["n=490154203237519", "n=490154203237519"],
  ["steam 76561198012345678", "steam <steam-id>"],
  ["username=carlos92 ok", "username=<account> ok"],
  ["Authorization: Bearer abcdefghijklmnop123", "Authorization: Bearer <token>"],
  ["t ghp_abcdefghijklmnopqrstuvwxyz0123456789", "t <token>"],
  ["api_key=sk-12345678&x=1", "api_key=<token>&x=1"],
  ['password: "hunter2"', 'password: "<token>"'],
];

test("redact: mismas reglas que la app", () => {
  for (const [input, expected] of cases) assert.equal(redact(input), expected, input);
});

test("redact: deja intacto un stack trace", () => {
  const trace = [
    "java.lang.IllegalStateException: boom",
    "    at com.murdoc820.hyperplay.GameSessionActivity.onCreate(GameSessionActivity.kt:123)",
    "#00 pc 000000000004b1c0  /data/app/~~x/lib/arm64/libhyperplay.so (Translator::run+64)",
    "exit code 3221225477, 0xC0000005 at 0x7f3a2b1c00",
  ].join("\n");
  assert.equal(redact(trace), trace);
});

test("redactReport: metadatos intactos, texto libre redactado", () => {
  const out = redactReport({
    id: "5a1c1f6e-3c9b-4e43-9f1e-1e5e3c2d9a10", kind: "juego", appVersion: "1.2.3.4", appVersionCode: 42,
    details: "Juego: /storage/emulated/0/Pepe/Celeste/Celeste.exe", logcat: "D/net: 10.0.0.5 me@example.org",
  });
  assert.equal(out.appVersion, "1.2.3.4");
  assert.equal(out.appVersionCode, 42);
  assert.equal(out.details, "Juego: <storage>/…/Celeste/Celeste.exe");
  assert.equal(out.logcat, "D/net: <ip> <email>");
});

test("reportsRepo: REPORTS_REPO manda y se valida", () => {
  assert.equal(reportsRepo({ REPORTS_REPO: "me/HyperPlay-Reports", GITHUB_REPO: "me/HyperPlay" }), "me/HyperPlay-Reports");
  assert.equal(reportsRepo({ GITHUB_REPO: "me/HyperPlay" }), "me/HyperPlay");
  assert.equal(reportsRepo({ REPORTS_REPO: "../../evil" }), "");
  assert.equal(reportsRepo({}), "");
});

test("worker: el Issue sale redactado y va a REPORTS_REPO", async () => {
  const calls = [];
  const realFetch = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    calls.push({ url, body: JSON.parse(init.body) });
    return new Response(JSON.stringify({ number: 7 }), { status: 201 });
  };
  try {
    const report = {
      id: "abc", kind: "juego", title: "Celeste: exit code / código 1", appVersion: "0.9.0",
      details: "Juego: /storage/emulated/0/Pepe/Juegos/Celeste/Celeste.exe desde 192.168.0.10",
      logcat: "user me@example.org android_id=9774d56d682e549c",
    };
    const res = await worker.fetch(
      new Request("https://w.example/report", { method: "POST", body: JSON.stringify(report) }),
      { REPORTS_REPO: "me/HyperPlay-Reports", GITHUB_REPO: "me/HyperPlay", GITHUB_TOKEN: "x" },
    );
    assert.equal(res.status, 200);
    assert.equal(calls[0].url, "https://api.github.com/repos/me/HyperPlay-Reports/issues");
    const body = calls[0].body.body;
    for (const leak of ["Pepe", "192.168.0.10", "me@example.org", "9774d56d682e549c"]) {
      assert.ok(!body.includes(leak), `se filtró ${leak}`);
    }
    assert.ok(body.includes("<storage>/…/Celeste/Celeste.exe"));
  } finally {
    globalThis.fetch = realFetch;
  }
});
