// node tools/inspect/shot.mjs <name> "<query>"  -> tools/inspect/<name>.png (Server: npx vite auf Port 5180)
import puppeteer from "puppeteer-core";
const [name, q = ""] = process.argv.slice(2);
const b = await puppeteer.launch({ executablePath: "/usr/bin/chromium", args: ["--no-sandbox", "--ozone-platform=headless", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"], headless: true });
const p = await b.newPage(); p.on("pageerror", (e) => console.log("FEHLER", e.message)); p.on("console", (m) => m.type() === "error" && console.log("KONSOLE", m.text()));
const w = +(new URLSearchParams(q).get("w") || 1200), h = +(new URLSearchParams(q).get("h") || 900);
await p.setViewport({ width: w, height: h });
await p.goto(`http://127.0.0.1:5180/tools/inspect/index.html?${q}`);
await p.waitForFunction(() => window.fertig, { timeout: 120000 });
if (process.env.INFO) console.log((await p.evaluate(() => window.info)).join("\n"));
await p.screenshot({ path: `tools/inspect/${name}.png` }); await b.close();
