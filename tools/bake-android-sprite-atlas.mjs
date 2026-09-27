import { execFileSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { parseArgs } from "node:util";
import { chromium } from "playwright-core";
import { gotoScenePage } from "../dist/src/browser-harness.js";
import { createSuiteSceneServer } from "../dist/src/capture-suite-reference.js";
import { parseDataUrl } from "../dist/src/data-url.js";
import { executedModuleScript } from "../dist/src/executed-module-script.js";
import {
    isRecord,
    isNonemptyString,
    recordOf,
} from "../dist/src/json-fields.js";
import {
    createSpriteAtlasBundle,
    spriteAtlasSourceIdentity,
    spriteAtlasRequestPaths,
    spriteAtlasProducerIdentity,
} from "../dist/src/sprite-atlas-bundle.js";

const { values } = parseArgs({
    options: {
        adb: { type: "string" },
        device: { type: "string" },
        socket: { type: "string" },
        "browser-package": { type: "string" },
        output: { type: "string" },
        module: { type: "string", multiple: true },
    },
});
if (
    !values.adb ||
    !values.device ||
    !values.socket ||
    !values["browser-package"] ||
    !values.output ||
    !values.module?.length
)
    throw new Error(
        "Use --adb, --device, --socket, --browser-package, --output and --module path.ts#factory (repeatable). Connects to an already running browser; owns only its pages and forwarding rules.",
    );
const adbPath = values.adb;
const serial = values.device;
const socket = values.socket;
const browserPackage = values["browser-package"];
if (
    !/^[\w.]+$/.test(socket) ||
    !/^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$/.test(browserPackage)
)
    throw new Error("Invalid browser socket or package.");
/** @param {string[]} args */
function adb(...args) {
    return execFileSync(adbPath, ["-s", serial, ...args], {
        encoding: "utf8",
        timeout: 60000,
        windowsHide: true,
    }).trim();
}
const sources = values.module.map((entry) => {
    const separator = entry.lastIndexOf("#");
    if (separator <= 0) throw new Error("--module requires path.ts#factory.");
    return spriteAtlasSourceIdentity(
        resolve(entry.slice(0, separator)),
        entry.slice(separator + 1),
    );
});
const output = resolve(values.output);
const producerIdentity = spriteAtlasProducerIdentity();
const port = adb("forward", "tcp:0", `localabstract:${socket}`);
if (!/^\d+$/.test(port))
    throw new Error("ADB did not allocate a forwarding port.");
/** @type {import('playwright-core').Browser | undefined} */
let browser;
/** @type {import('../src/sprite-atlas-bundle.js').AtlasDevice | undefined} */
let device;
/** @type {{source: ReturnType<typeof spriteAtlasSourceIdentity>, bytes: Uint8Array}[]} */
const entries = [];
try {
    browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`, {
        noDefaults: true,
    });
    const client = await browser.newBrowserCDPSession();
    /** @type {unknown} */
    const version = await client.send("Browser.getVersion");
    /** @type {unknown} */
    const system = await client.send("SystemInfo.getInfo");
    /** @type {unknown} */
    const processes = await client.send("SystemInfo.getProcessInfo");
    const pids = adb("shell", "pidof", browserPackage).split(/\s+/).map(Number);
    if (
        !isRecord(processes) ||
        !Array.isArray(processes.processInfo) ||
        !processes.processInfo.some(
            (entry) =>
                isRecord(entry) &&
                entry.type === "browser" &&
                typeof entry.id === "number" &&
                pids.includes(entry.id),
        )
    )
        throw new Error(
            "The CDP browser process does not belong to the selected Android package.",
        );
    if (
        !recordOf(version, isNonemptyString) ||
        !isRecord(system) ||
        !isRecord(system.gpu) ||
        !Array.isArray(system.gpu.devices) ||
        !system.gpu.devices.length ||
        !isNonemptyString(system.commandLine) ||
        !system.commandLine
            .split(/\s+/)
            .includes(`--remote-debugging-socket-name=${socket}`)
    )
        throw new Error(
            "CDP did not provide complete browser/GPU/command-line identity.",
        );
    /** @type {Record<string, string>} */
    const browserApks = {};
    for (const line of adb("shell", "pm", "path", browserPackage).split(
        /\r?\n/,
    )) {
        if (!line.startsWith("package:/"))
            throw new Error("Invalid Android browser APK path.");
        const path = line.slice("package:".length);
        const checksum = adb("shell", "sha256sum", path).split(/\s+/)[0];
        if (!checksum || !/^[a-f0-9]{64}$/.test(checksum))
            throw new Error("Cannot identify browser APK bytes.");
        browserApks[path] = checksum;
    }
    const context = browser.contexts()[0];
    if (!context)
        throw new Error("The selected browser has no default context.");
    for (const source of sources) {
        const server = createSuiteSceneServer(
            executedModuleScript(source.module, source.exportName),
        );
        /** @type {import('playwright-core').Page | undefined} */
        let page;
        /** @type {number | undefined} */
        let reversePort;
        try {
            await new Promise((resolveListen, reject) => {
                server.once("error", reject);
                server.listen(0, "127.0.0.1", () => resolveListen(undefined));
            });
            const address = server.address();
            if (!address || typeof address === "string")
                throw new Error("Atlas server has no port.");
            adb(
                "reverse",
                "--no-rebind",
                `tcp:${address.port}`,
                `tcp:${address.port}`,
            );
            reversePort = address.port;
            page = await context.newPage();
            const origin = `http://127.0.0.1:${address.port}`;
            const allowedPaths = new Set(spriteAtlasRequestPaths(source));
            /** @type {string[]} */
            const undeclared = [];
            await page.route("**/*", async (route) => {
                const url = new URL(route.request().url());
                if (
                    url.origin === origin &&
                    allowedPaths.has(decodeURIComponent(url.pathname))
                )
                    await route.continue();
                else {
                    undeclared.push(url.href);
                    await route.abort();
                }
            });
            await gotoScenePage(page, origin);
            await page.waitForFunction(
                "typeof window.__runModuleExport === 'function'",
            );
            /** @type {unknown} */
            const result = await page.evaluate("window.__runModuleExport()");
            if (undeclared.length)
                throw new Error(
                    `Atlas factory requested undeclared inputs: ${undeclared.join(", ")}`,
                );
            const payload =
                typeof result === "string" ? parseDataUrl(result) : undefined;
            if (!payload || payload.mediaType !== "image/png")
                throw new Error("Atlas factory did not return PNG data.");
            const deviceScaleFactor = await page.evaluate(
                () => globalThis.devicePixelRatio,
            );
            if (device && device.deviceScaleFactor !== deviceScaleFactor)
                throw new Error(
                    "Device scale changed during bundle production.",
                );
            device = {
                serial: adb("get-serialno"),
                buildFingerprint: adb(
                    "shell",
                    "getprop",
                    "ro.build.fingerprint",
                ),
                model: adb("shell", "getprop", "ro.product.model"),
                api: adb("shell", "getprop", "ro.build.version.sdk"),
                browserPackage,
                browserApks,
                browserVersion: version,
                commandLine: system.commandLine,
                gpu: JSON.stringify(system.gpu),
                deviceScaleFactor,
            };
            entries.push({ source, bytes: payload.bytes });
        } finally {
            try {
                await page?.close();
            } finally {
                try {
                    if (reversePort !== undefined)
                        adb("reverse", "--remove", `tcp:${reversePort}`);
                } finally {
                    server.closeAllConnections();
                    if (server.listening)
                        await new Promise((done) => server.close(done));
                }
            }
        }
    }
} finally {
    try {
        await browser?.close();
    } finally {
        adb("forward", "--remove", `tcp:${port}`);
    }
}
if (!device) throw new Error("No device atlas was produced.");
if (producerIdentity !== spriteAtlasProducerIdentity())
    throw new Error("Atlas producer inputs changed during capture.");
const bundle = createSpriteAtlasBundle(device, entries);
mkdirSync(dirname(output), { recursive: true });
writeFileSync(output, bundle, { flag: "wx" });
console.log(`Wrote ${entries.length} verified atlas entries to ${output}.`);
