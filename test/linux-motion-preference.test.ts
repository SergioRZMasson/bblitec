import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync } from "node:fs";
import { resolve } from "node:path";
import test from "node:test";
import { discoverDevelopmentTools } from "../src/development-tools.js";

test(
    "Linux motion preference validates portal replies and refreshes its shared cache",
    { skip: process.platform !== "linux" },
    () => {
        const tools = discoverDevelopmentTools();
        assert.ok(tools.cxx, "A C++ compiler is required.");
        const directory = resolve("artifacts/test-linux-motion-preference");
        mkdirSync(directory, { recursive: true });
        const executable = resolve(directory, "check");
        const flags = execFileSync(
            "pkg-config",
            ["--cflags", "--libs", "dbus-1"],
            {
                encoding: "utf8",
            },
        )
            .trim()
            .split(/\s+/);
        execFileSync(
            tools.cxx,
            [
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Werror",
                "-pthread",
                "-I",
                resolve("native/src"),
                "test/fixtures/linux-motion-preference-check.cpp",
                "native/src/pal_system_preferences_linux.cpp",
                ...flags,
                "-o",
                executable,
            ],
            { windowsHide: true, stdio: "pipe" },
        );
        const output = execFileSync("dbus-run-session", ["--", executable], {
            windowsHide: true,
            encoding: "utf8",
            timeout: 15_000,
        });
        assert.match(
            output,
            /portal values, strict errors, timeout and cache refresh passed/,
        );
    },
);
