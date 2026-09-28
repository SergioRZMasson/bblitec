import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync } from "node:fs";
import { resolve } from "node:path";
import test from "node:test";
import { discoverDevelopmentTools } from "../src/development-tools.js";

test(
    "macOS motion preference follows NSWorkspace and refreshes its shared cache",
    { skip: process.platform !== "darwin" },
    () => {
        const tools = discoverDevelopmentTools();
        assert.ok(tools.cxx, "An Objective-C++ compiler is required.");
        const directory = resolve("artifacts/test-macos-motion-preference");
        mkdirSync(directory, { recursive: true });
        const executable = resolve(directory, "check");
        execFileSync(
            tools.cxx,
            [
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Werror",
                "-fobjc-arc",
                "-I",
                resolve("native/src"),
                "test/fixtures/macos-motion-preference-check.mm",
                "native/src/pal_system_preferences_macos.mm",
                "-framework",
                "AppKit",
                "-o",
                executable,
            ],
            { windowsHide: true, stdio: "pipe" },
        );
        const output = execFileSync(executable, [], {
            windowsHide: true,
            encoding: "utf8",
        });
        assert.match(output, /live preference and cache refresh passed/);
    },
);
