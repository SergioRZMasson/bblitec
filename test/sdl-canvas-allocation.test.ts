import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync } from "node:fs";
import { resolve } from "node:path";
import test from "node:test";
import {
    nativeFixtureVcpkgRoot,
    optionalNativeFixtureTools,
    runNativeFixtureCompiler,
} from "./native-fixture.js";

const native = optionalNativeFixtureTools();

test(
    "SDL canvas allocation retains public texture usages and gates private properties",
    { skip: !native },
    () => {
        const directory = resolve("artifacts/sdl-canvas-allocation");
        mkdirSync(directory, { recursive: true });
        const executable = resolve(directory, "check.exe");
        runNativeFixtureCompiler(native!, [
            "/nologo",
            "/std:c++20",
            "/W4",
            "/WX",
            "/EHsc",
            "/MD",
            "/Inative/include",
            "/Inative/src",
            `/external:I${nativeFixtureVcpkgRoot}/include`,
            "/external:W0",
            "test/fixtures/sdl-canvas-allocation-check.cpp",
            `/Fo:${directory}/`,
            `/Fe:${executable}`,
        ]);
        assert.equal(
            execFileSync(executable, { encoding: "utf8", windowsHide: true }),
            "",
        );
    },
);
