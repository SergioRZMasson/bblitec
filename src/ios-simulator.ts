import type { SceneDefinition } from "./scene-registry.js";
import {
    measuredRunEnvironment,
    type MeasuredRunOptions,
} from "./tooling/native-run.js";

export interface IosSimulator {
    udid: string;
    name: string;
    state: string;
    runtime: string;
}

export function selectIosSimulator(
    json: string,
    requested: string,
): IosSimulator {
    const root: unknown = JSON.parse(json);
    if (
        !root ||
        typeof root !== "object" ||
        !("devices" in root) ||
        !root.devices ||
        typeof root.devices !== "object" ||
        Array.isArray(root.devices)
    ) {
        throw new Error("simctl returned no device inventory.");
    }
    const matches: IosSimulator[] = [];
    for (const [runtime, entries] of Object.entries(root.devices)) {
        if (!runtime.startsWith("com.apple.CoreSimulator.SimRuntime.iOS-"))
            continue;
        if (!Array.isArray(entries))
            throw new Error("simctl returned an invalid iOS device list.");
        const devices: unknown[] = entries;
        for (const device of devices) {
            if (
                !device ||
                typeof device !== "object" ||
                !("isAvailable" in device) ||
                typeof device.isAvailable !== "boolean" ||
                !("udid" in device) ||
                typeof device.udid !== "string" ||
                !("name" in device) ||
                typeof device.name !== "string" ||
                !("state" in device) ||
                typeof device.state !== "string"
            ) {
                throw new Error("simctl returned an invalid iOS device.");
            }
            if (
                device.isAvailable &&
                (requested === "booted"
                    ? device.state === "Booted"
                    : device.udid === requested)
            ) {
                matches.push({
                    udid: device.udid,
                    name: device.name,
                    state: device.state,
                    runtime,
                });
            }
        }
    }
    if (matches.length !== 1) {
        throw new Error(
            `Expected one available iOS simulator for '${requested}', found ${matches.length}. Use an explicit UDID from xcrun simctl list devices available.`,
        );
    }
    return matches[0]!;
}

export function iosCaptureEnvironment(
    scene: SceneDefinition,
    backend: "sdl_gpu" | "dawn",
    options: Pick<
        MeasuredRunOptions,
        "frame" | "maxFrames" | "tape" | "testPass"
    > & {
        canvasOnly?: boolean;
        runtimeTrace?: boolean;
    } = {},
): Record<string, string> {
    if (
        options.maxFrames !== undefined &&
        (options.frame === undefined ||
            !Number.isSafeInteger(options.maxFrames) ||
            options.maxFrames <= options.frame ||
            options.maxFrames > 1000001)
    )
        throw new Error(
            "--max-frames requires --frame and an integer in [frame + 1, 1000001].",
        );
    return measuredRunEnvironment({
        ...options,
        environment: scene.parity?.nativeEnvironment ?? {},
        backend,
        extra: {
            BBLITE_GPU_BACKEND: backend,
            BBLITE_GPU_DEBUG: "1",
            ...(options.canvasOnly ? { BBLITE_CAPTURE_UI: "0" } : {}),
            ...(options.runtimeTrace ? { BBLITE_RUNTIME_TRACE: "1" } : {}),
        },
    });
}

export function verifyIosNativeExit(log: string, runId: string): void {
    const exits = [
        ...log.matchAll(/^Native exit: (\d+) run=(\S+)\r?$/gm),
    ].filter((match) => match[2] === runId);
    if (exits.length !== 1)
        throw new Error(
            "The native app did not report one exit for this run; inspect simulator.log.",
        );
    if (exits[0]![1] !== "0")
        throw new Error(
            `The native app exited with status ${exits[0]![1]}; inspect simulator.log.`,
        );
}

/** simctl prints the application's PID after launching or foregrounding it. */
export function iosLaunchPid(log: string, applicationId: string): number {
    const prefix = `${applicationId}: `;
    const lines = log.split(/\r?\n/).filter((line) => line.startsWith(prefix));
    const value = lines.length === 1 ? lines[0]!.slice(prefix.length) : "";
    const pid = Number(value);
    if (!/^\d+$/.test(value) || !Number.isSafeInteger(pid) || pid <= 0)
        throw new Error("simctl did not report one valid application PID.");
    return pid;
}

/** Completed native frames, independent of Simulator launch/UI readiness. */
export function iosRenderedFrames(log: string): Record<string, number> {
    const frames: Record<string, number> = {};
    for (const match of log.matchAll(
        /\[mem\]\[frame\] engine=(\d+) frame=(\d+) /g,
    ))
        frames[match[1]!] = Number(match[2]);
    return frames;
}

export function verifyIosFrameProgress(
    before: Readonly<Record<string, number>>,
    after: Readonly<Record<string, number>>,
): void {
    if (Object.keys(before).length === 0)
        throw new Error(
            "No native rendering was reported before backgrounding.",
        );
    for (const [engine, frame] of Object.entries(before)) {
        const resumed = after[engine];
        if (resumed === undefined || resumed <= frame)
            throw new Error(
                `Engine ${engine} reported no rendering progress after foregrounding.`,
            );
    }
}
