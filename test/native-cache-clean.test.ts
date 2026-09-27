import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
    existsSync,
    mkdirSync,
    mkdtempSync,
    rmSync,
    symlinkSync,
    utimesSync,
    writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import test from "node:test";
import {
    oldNativeCacheEntries,
    pruneNativeCache,
} from "../src/native-cache-clean.js";

test("native support cleanup expires complete keys and partial files while retaining reuse, ccache and locks", (t) => {
    const root = mkdtempSync(join(tmpdir(), "bblite-cache-clean-"));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    const old = new Date(Date.now() - 60 * 86_400_000);
    const write = (name: string, date = old): string => {
        const path = join(root, name);
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(path, "cache contents");
        utimesSync(path, date, date);
        return path;
    };
    const header = `headers/${"a".repeat(64)}`;
    write(`${header}/bblite/generated.hpp`);
    write(`${header}/complete`);
    utimesSync(join(root, header, "bblite"), old, old);
    utimesSync(join(root, header), old, old);
    const oldLock = write(`${header}.lock`);
    const source = write("sources/module-0123456789abcdef.cpp");
    const partial = write("sources/partial-0123456789abcdef.cpp.partial");
    const pch = write("pch/bblite_pch-0123456789abcdef.cxx");
    const reused = write("sources/used-0123456789abcdef.cpp");
    write("sources/used-0123456789abcdef.cpp.lock", new Date());
    const recent = write("sources/new-0123456789abcdef.cpp", new Date());
    const object = write("a/cache-object");
    const unknown = write("sources/unknown.txt");
    const expected = [join(root, header), source, partial, pch].sort();
    assert.deepEqual(
        oldNativeCacheEntries(root)
            .map((entry) => entry.path)
            .sort(),
        expected,
    );
    assert.ok(expected.every(existsSync), "report is read-only");
    assert.deepEqual(
        pruneNativeCache(root)
            .map((entry) => entry.path)
            .sort(),
        expected,
    );
    assert.ok(expected.every((path) => !existsSync(path)));
    assert.ok([oldLock, reused, recent, object, unknown].every(existsSync));
    assert.throws(() => oldNativeCacheEntries(root, 0), /positive/);
    assert.throws(() => oldNativeCacheEntries(root, Number.NaN), /positive/);
});

test("native support cleanup never follows root, family or nested directory links", (t) => {
    const root = mkdtempSync(join(tmpdir(), "bblite-cache-links-"));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    const target = join(root, "target");
    mkdirSync(target);
    const link = (path: string): void =>
        symlinkSync(
            target,
            path,
            process.platform === "win32" ? "junction" : "dir",
        );
    link(join(root, "linked-root"));
    assert.deepEqual(oldNativeCacheEntries(join(root, "linked-root")), []);
    const cache = join(root, "cache");
    mkdirSync(cache);
    link(join(cache, "sources"));
    mkdirSync(join(cache, "headers", "a".repeat(64)), { recursive: true });
    link(join(cache, "headers", "a".repeat(64), "linked-input"));
    assert.deepEqual(pruneNativeCache(cache, Number.MIN_VALUE), []);
    assert.ok(existsSync(target));
});

test("normal clean reports and prunes old support inputs with a configurable age", (t) => {
    const root = mkdtempSync(join(tmpdir(), "bblite-clean-command-"));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    const input = join(
        root,
        "artifacts/native-cache/sources/module-0123456789abcdef.cpp",
    );
    mkdirSync(dirname(input), { recursive: true });
    writeFileSync(input, "cached unit");
    const old = new Date(Date.now() - 60 * 86_400_000);
    utimesSync(input, old, old);
    const run = (...args: string[]): string =>
        execFileSync(
            process.execPath,
            [resolve("dist/src/scene-command.js"), "clean", ...args],
            { cwd: root, encoding: "utf8" },
        );
    assert.match(run("--report"), /native-cache support inputs: 1 expired/);
    assert.ok(existsSync(input));
    run("--artifacts", "--cache-days", "90");
    assert.ok(existsSync(input));
    assert.match(run("--artifacts"), /pruned 1 old native support inputs/);
    assert.ok(!existsSync(input));
});
