import { spawn, spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";
import {
    copyFileSync,
    existsSync,
    mkdirSync,
    readFileSync,
    writeFileSync,
} from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { setTimeout as delay } from "node:timers/promises";
import { PNG } from "pngjs";
import {
    iosCaptureEnvironment,
    iosRenderedFrames,
    iosLaunchPid,
    selectIosSimulator,
    verifyIosNativeExit,
    verifyIosFrameProgress,
} from "../dist/src/ios-simulator.js";
import { resolveScene } from "../dist/src/scene-registry.js";
import {
    verifyBuildIdentity,
    verifyDeployedPayload,
} from "../dist/src/tooling/native-run.js";
import {
    contentFingerprint,
    writeJsonRecord,
} from "../dist/src/tooling/records.js";
import { parseBackendName } from "../dist/src/tooling/backends.js";
import { expandTape } from "../dist/src/tooling/check-spec.js";

/** @import { IosSimulator } from "../dist/src/ios-simulator.js" */

const { values } = parseArgs({
    options: {
        scene: { type: "string" },
        device: { type: "string" },
        bundle: { type: "string" },
        "build-directory": { type: "string" },
        output: { type: "string" },
        app: { type: "string", default: "org.bblite.prototype" },
        backend: { type: "string", default: "dawn" },
        frame: { type: "string" },
        "max-frames": { type: "string" },
        "runtime-trace": { type: "boolean", default: false },
        replay: { type: "string" },
        lifecycle: { type: "boolean", default: false },
        "render-state": { type: "boolean", default: false },
        "canvas-only": { type: "boolean", default: false },
    },
});
if (process.platform !== "darwin")
    throw new Error("iOS Simulator smoke requires macOS and Xcode.");
if (!values.scene || !values.device || !values.bundle || !values.output) {
    throw new Error("Use --scene, --device, --bundle and --output.");
}
if (!/^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$/.test(values.app))
    throw new Error("Invalid application ID.");
const backend = parseBackendName(values.backend, "--backend", false);
const frame = values.frame === undefined ? undefined : Number(values.frame);
if (
    frame !== undefined &&
    (!Number.isSafeInteger(frame) || frame < 0 || frame > 1000000)
) {
    throw new Error("--frame must be an integer in [0, 1000000].");
}
if (values.lifecycle && (frame === undefined || frame < 60))
    throw new Error("--lifecycle requires an explicit --frame of at least 60.");
if (
    values.replay !== undefined &&
    (frame === undefined || !values.replay.trim())
) {
    throw new Error(
        "--replay requires a nonempty input tape and an explicit --frame.",
    );
}
const output = resolve(values.output);
mkdirSync(output, { recursive: true });
let log = "";
/**
 * @param {string[]} args
 * @param {NodeJS.ProcessEnv} environment
 */
function simctl(args, environment = process.env, timeout = 30000) {
    const result = spawnSync("xcrun", ["simctl", ...args], {
        encoding: "utf8",
        env: environment,
        timeout,
        maxBuffer: 16 * 1024 * 1024,
    });
    log += `$ xcrun simctl ${args.join(" ")}\n${result.stdout ?? ""}${result.stderr ?? ""}`;
    if (result.error) throw result.error;
    if (result.status !== 0)
        throw new Error(
            `simctl ${args[0]} failed (${result.status ?? result.signal}); inspect simulator.log.`,
        );
    return result.stdout.trim();
}

/**
 * @param {string[]} args
 * @param {NodeJS.ProcessEnv} environment
 * @param {string} device
 * @param {string} applicationId
 * @param {string} screenshot
 */
async function lifecycleLaunch(
    args,
    environment,
    device,
    applicationId,
    screenshot,
) {
    log += `$ xcrun simctl ${args.join(" ")}\n`;
    const child = spawn("xcrun", ["simctl", ...args], {
        env: environment,
        timeout: 120000,
    });
    let output = "";
    let finished = false;
    /** @type {Error | undefined} */
    let spawnError;
    for (const stream of [child.stdout, child.stderr]) {
        stream.setEncoding("utf8");
        stream.on("data", (chunk) => {
            output += String(chunk);
            log += String(chunk);
        });
    }
    child.on("error", (error) => {
        spawnError = error;
    });
    const completion = new Promise((resolve) => {
        child.on("close", (status, signal) => {
            finished = true;
            resolve({ status, signal });
        });
    });
    try {
        const deadline = Date.now() + 30000;
        while (Object.keys(iosRenderedFrames(output)).length === 0) {
            if (spawnError) throw spawnError;
            if (finished || Date.now() > deadline)
                throw new Error(
                    "The app did not render before the lifecycle transition.",
                );
            await delay(25);
        }
        const pid = iosLaunchPid(
            simctl(["launch", device, applicationId]),
            applicationId,
        );
        const framesBeforeBackground = iosRenderedFrames(output);
        simctl(["launch", device, "com.apple.Preferences"]);
        await delay(500);
        if (finished || existsSync(screenshot))
            throw new Error(
                "The capture finished before foregrounding; increase --frame.",
            );
        const resumeLogStart = output.length;
        const resumedPid = iosLaunchPid(
            simctl(["launch", device, applicationId]),
            applicationId,
        );
        if (resumedPid !== pid)
            throw new Error(
                `The app restarted instead of resuming (${pid} -> ${resumedPid}).`,
            );
        const result = await completion;
        if (spawnError) throw spawnError;
        if (result.status !== 0)
            throw new Error(
                `Simulator lifecycle launch failed (${result.status ?? result.signal}); inspect simulator.log.`,
            );
        const framesAfterResume = iosRenderedFrames(
            output.slice(resumeLogStart),
        );
        verifyIosFrameProgress(framesBeforeBackground, framesAfterResume);
        return { pid, resumedPid, framesBeforeBackground, framesAfterResume };
    } finally {
        if (!finished) {
            child.kill();
            await completion;
        }
    }
}
/**
 * @type {{
 *     platform: string,
 *     scene: string,
 *     backend: string,
 *     runId: string,
 *     bundle: string,
 *     bundleSha256: string,
 *     passed: boolean,
 *     device?: IosSimulator,
 *     environment?: Record<string, string>,
 *     width?: number,
 *     height?: number,
 *     lifecycle?: { pid: number, resumedPid: number, framesBeforeBackground: Record<string, number>, framesAfterResume: Record<string, number> },
 *     error?: string,
 * }}
 */
const receipt = {
    platform: "ios-simulator",
    scene: values.scene,
    backend,
    runId: randomUUID(),
    bundle: resolve(values.bundle),
    bundleSha256: contentFingerprint([values.bundle]),
    passed: false,
};
/** @type {string | undefined} */
let launchedDevice;
try {
    const scene = resolveScene(values.scene);
    const captureEnvironment = iosCaptureEnvironment(scene, backend, {
        ...(frame === undefined ? {} : { frame }),
        ...(values["max-frames"] === undefined
            ? {}
            : { maxFrames: Number(values["max-frames"]) }),
        canvasOnly: values["canvas-only"],
        runtimeTrace: values["runtime-trace"],
        ...(values.replay === undefined
            ? {}
            : {
                  tape: expandTape(values.replay.split(",")),
                  testPass: false,
              }),
    });
    const executable = join(receipt.bundle, "bblite_native");
    verifyDeployedPayload(executable, scene.output, values["build-directory"]);
    const device = selectIosSimulator(
        simctl(["list", "devices", "available", "--json"]),
        values.device,
    );
    receipt.device = device;
    simctl(["bootstatus", device.udid, "-b"], process.env, 180000);
    simctl(["install", device.udid, receipt.bundle]);
    const container = simctl([
        "get_app_container",
        device.udid,
        values.app,
        "data",
    ]);
    const screenshot = join(container, "Documents", `${receipt.runId}.png`);
    const stamp = join(container, "Documents", `${receipt.runId}.stamp`);
    const renderState = join(container, "Documents", `${receipt.runId}.json`);
    const environment = {
        ...captureEnvironment,
        BBLITE_RUN_ID: receipt.runId,
        BBLITE_SCREENSHOT: screenshot,
        BBLITE_BUILD_STAMP_OUT: stamp,
        ...(values.lifecycle
            ? { BBLITE_MEM_PROFILE: "1", BBLITE_TEST_PASS: "0" }
            : {}),
        ...(values["render-state"]
            ? { BBLITE_RENDER_CAPTURE: renderState }
            : {}),
        SDL_ASSERT: "abort",
    };
    receipt.environment = environment;
    const launchEnvironment = Object.fromEntries(
        Object.entries(process.env).filter(
            ([key]) => !key.startsWith("SIMCTL_CHILD_"),
        ),
    );
    for (const [key, value] of Object.entries(environment))
        launchEnvironment[`SIMCTL_CHILD_${key}`] = value;
    const start = log.length;
    launchedDevice = device.udid;
    const launchArguments = [
        "launch",
        "--console",
        "--terminate-running-process",
        device.udid,
        values.app,
    ];
    if (values.lifecycle)
        receipt.lifecycle = await lifecycleLaunch(
            launchArguments,
            launchEnvironment,
            device.udid,
            values.app,
            screenshot,
        );
    else simctl(launchArguments, launchEnvironment, 120000);
    launchedDevice = undefined;
    verifyIosNativeExit(log.slice(start), receipt.runId);
    verifyBuildIdentity(executable, scene.output, stamp);
    const png = PNG.sync.read(readFileSync(screenshot));
    copyFileSync(screenshot, join(output, "capture.png"));
    copyFileSync(stamp, join(output, "build-stamp.txt"));
    if (values["render-state"])
        copyFileSync(renderState, join(output, "render.json"));
    Object.assign(receipt, {
        passed: true,
        width: png.width,
        height: png.height,
    });
    console.log(
        `iOS Simulator smoke passed: ${values.scene}, ${backend}, ${device.name}, ${png.width}x${png.height}. ${output}`,
    );
} catch (error) {
    receipt.error = error instanceof Error ? error.message : String(error);
    throw error;
} finally {
    if (launchedDevice) {
        try {
            simctl(["terminate", launchedDevice, values.app]);
        } catch (error) {
            console.error(
                `Simulator cleanup: ${error instanceof Error ? error.message : String(error)}`,
            );
        }
    }
    writeFileSync(join(output, "simulator.log"), log);
    writeJsonRecord(join(output, "report.json"), receipt);
}
