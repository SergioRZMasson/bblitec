import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { afterEach, beforeEach, test } from "node:test";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";
import { drawSpriteAtlasPng } from "../src/executed-module-assets.js";
import {
    createSpriteAtlasBundle,
    configuredSpriteAtlasBundle,
    spriteAtlasBundleVariable,
    spriteAtlasSourceIdentity,
    spriteAtlasRequestPaths,
    type AtlasDevice,
} from "../src/sprite-atlas-bundle.js";
import {
    generationIsCurrent,
    generationStampPath,
    recordGeneration,
} from "../src/generation-stamp.js";

const directory = resolve(".cache", "sprite-atlas-bundle-test");
const source = resolve(directory, "atlas.ts");
const sibling = resolve(directory, "color.ts");
const bundlePath = resolve(directory, "bundle.json");
const scene = {
    id: "sprite-atlas-bundle-test",
    output: resolve(directory, "generated"),
};
const device: AtlasDevice = {
    serial: "test-device",
    buildFingerprint: "android/test/fingerprint",
    model: "test-model",
    api: "35",
    browserPackage: "org.chromium.chrome",
    browserApks: { "/data/app/base.apk": "a".repeat(64) },
    browserVersion: {
        product: "Chrome/156",
        revision: "revision",
        userAgent: "test-agent",
        jsVersion: "15",
        protocolVersion: "1.3",
    },
    commandLine: "--use-angle=vulkan",
    gpu: JSON.stringify({
        devices: [{ vendorString: "test", deviceString: "test" }],
        featureStatus: { "2d_canvas": "enabled" },
    }),
    deviceScaleFactor: 1,
};
let previous: string | undefined;

function png(color = 23): Buffer {
    const image = new PNG({ width: 2, height: 1 });
    image.data.set([color, 31, 41, 255, 11, 13, 17, 255]);
    return PNG.sync.write(image);
}
function produce(bytes = png(), target = device): string {
    return createSpriteAtlasBundle(target, [
        { source: spriteAtlasSourceIdentity(source, "makeAtlas"), bytes },
    ]);
}
function writeBundle(text = produce()): void {
    writeFileSync(bundlePath, text);
    process.env[spriteAtlasBundleVariable] = bundlePath;
}
async function consume(): Promise<
    Awaited<ReturnType<typeof drawSpriteAtlasPng>>
> {
    return drawSpriteAtlasPng({ modulePath: source, exportName: "makeAtlas" });
}
function stamp(): void {
    const atlas = configuredSpriteAtlasBundle();
    writeFileSync(
        resolve(scene.output, "manifest.json"),
        JSON.stringify({
            inputs: [],
            assets: [
                {
                    kind: "sprite-atlas",
                    ...(atlas
                        ? {
                              spriteAtlasProducer: {
                                  bundleSha256: atlas.identity,
                              },
                          }
                        : {}),
                },
            ],
        }),
    );
    assert.equal(
        recordGeneration(scene, [], Date.now() + 1000, { durationMs: 1 }),
        true,
    );
    assert.equal(generationIsCurrent(scene, []), true);
}

beforeEach(() => {
    previous = process.env[spriteAtlasBundleVariable];
    delete process.env[spriteAtlasBundleVariable];
    mkdirSync(scene.output, { recursive: true });
    writeFileSync(
        source,
        'import { color } from "./color.js"; export function makeAtlas() { return color; }',
    );
    writeFileSync(sibling, 'export const color = "fixture";');
    writeFileSync(resolve(scene.output, "main.cpp"), "int main() {}\n");
});
afterEach(() => {
    if (previous === undefined) delete process.env[spriteAtlasBundleVariable];
    else process.env[spriteAtlasBundleVariable] = previous;
    rmSync(directory, { recursive: true, force: true });
    rmSync(generationStampPath(scene.id), { force: true });
});

test("explicit atlas bundles preserve exact PNG bytes and consumed provenance without a host browser", async () => {
    assert.equal(configuredSpriteAtlasBundle(), undefined);
    writeBundle();
    const actual = await consume();
    assert.deepEqual(actual.bytes, png());
    assert.equal(
        actual.provenance?.bundleSha256,
        createHash("sha256").update(readFileSync(bundlePath)).digest("hex"),
    );
    assert.equal(actual.provenance?.source.exportName, "makeAtlas");
    assert.equal(Object.keys(actual.provenance.source.files).length, 2);
    assert.deepEqual(actual.provenance?.device, device);
});

test("bundle switches and same-path byte edits invalidate generation in one process", async () => {
    stamp();
    writeBundle();
    assert.equal(generationIsCurrent(scene, []), false);
    assert.equal(recordGeneration(scene, [], Date.now() + 1000), false);
    stamp();
    const first = (await consume()).provenance!.bundleSha256;
    writeBundle(
        produce(png(27), { ...device, buildFingerprint: "another-build" }),
    );
    assert.equal(generationIsCurrent(scene, []), false);
    const next = await consume();
    assert.notEqual(next.provenance!.bundleSha256, first);
    assert.deepEqual(next.bytes, png(27));
    // The output still describes the first bundle; don't stamp a replacement
    // selected while its child was running.
    assert.equal(recordGeneration(scene, [], Date.now() + 1000), false);
    stamp();
    delete process.env[spriteAtlasBundleVariable];
    assert.equal(generationIsCurrent(scene, []), false);
    assert.equal(recordGeneration(scene, [], Date.now() + 1000), false);
});

test("stale source, producer, pin, output and incomplete device records refuse before reuse", async () => {
    const original = produce();
    const changes = [
        original.replace('"sourceVersion": "', '"sourceVersion": "changed-'),
        original.replace(
            /"producerSha256": "[a-f0-9]+"/,
            `"producerSha256": "${"0".repeat(64)}"`,
        ),
        original.replace('"serial": "test-device"', '"serial": ""'),
        original.replace('"width": 2', '"width": 3'),
        original.replace(/"png": "[^"]+"/, '"png": "AAAA"'),
    ];
    for (const changed of changes) {
        writeBundle(changed);
        await assert.rejects(consume, /bundle|provenance/i);
    }
    writeBundle(original);
    await consume();
    stamp();
    writeFileSync(sibling, 'export const color = "edited";');
    await assert.rejects(consume, /source differs/);
    assert.throws(() => generationIsCurrent(scene, []));
});

test("missing, duplicate and unrequested bundle entries never fall back to host Chromium", async () => {
    writeBundle();
    await assert.rejects(
        () => drawSpriteAtlasPng({ modulePath: source, exportName: "other" }),
        /no entry/,
    );
    const entry = {
        source: spriteAtlasSourceIdentity(source, "makeAtlas"),
        bytes: png(),
    };
    assert.throws(
        () => createSpriteAtlasBundle(device, [entry, entry]),
        /Duplicate/,
    );
    rmSync(bundlePath);
    await assert.rejects(consume, /ENOENT/);
});

test("only closed repository-relative module imports are admitted", () => {
    writeFileSync(
        source,
        'import { color } from "./color"; export function makeAtlas() { return color; }',
    );
    const identity = spriteAtlasSourceIdentity(source, "makeAtlas");
    assert.ok(
        spriteAtlasRequestPaths(identity).includes(
            "/.cache/sprite-atlas-bundle-test/color",
        ),
    );
    assert.ok(
        spriteAtlasRequestPaths(identity).includes(
            "/.cache/sprite-atlas-bundle-test/color.js",
        ),
    );
    writeFileSync(
        source,
        'import "https://example.com/external.js"; export function makeAtlas() {}',
    );
    assert.throws(
        () => spriteAtlasSourceIdentity(source, "makeAtlas"),
        /repository-relative/,
    );
    writeFileSync(
        source,
        'const path = "./color.js"; export function makeAtlas() { return import(path); }',
    );
    assert.throws(
        () => spriteAtlasSourceIdentity(source, "makeAtlas"),
        /static import/,
    );
});

test("generation children inherit the selected bundle and reject stale input independently", () => {
    writeBundle();
    const module = pathToFileURL(
        resolve("dist/src/executed-module-assets.js"),
    ).href;
    const script = `import { drawSpriteAtlasPng } from ${JSON.stringify(module)}; const atlas = await drawSpriteAtlasPng(${JSON.stringify({ modulePath: source, exportName: "makeAtlas" })}); process.stdout.write(atlas.provenance.bundleSha256);`;
    const run = (): string =>
        execFileSync(process.execPath, ["--input-type=module", "-e", script], {
            encoding: "utf8",
            windowsHide: true,
            stdio: ["ignore", "pipe", "pipe"],
        });
    assert.equal(run(), configuredSpriteAtlasBundle()!.identity);
    writeFileSync(sibling, 'export const color = "edited";');
    assert.throws(run, /source differs/);
});

test("bundle identity is checkout-independent and detects transitive producer edits", () => {
    const roots = [resolve(directory, "left"), resolve(directory, "right")];
    for (const root of roots) {
        for (const path of [
            "tools/bake-android-sprite-atlas.mjs",
            "dist/src/lazy-helper.js",
            "package-lock.json",
            "node_modules/typescript/index.js",
            "node_modules/playwright-core/index.mjs",
            "node_modules/pngjs/package.json",
            "atlas.ts",
        ]) {
            const file = resolve(root, path);
            mkdirSync(dirname(file), { recursive: true });
            writeFileSync(file, "// identical fixture input");
        }
        mkdirSync(resolve(root, "upstream"), { recursive: true });
        writeFileSync(
            resolve(root, "upstream/babylon-lite.json"),
            readFileSync("upstream/babylon-lite.json"),
        );
    }
    const [left, right] = roots;
    assert.ok(left && right);
    const text = createSpriteAtlasBundle(
        device,
        [
            {
                source: spriteAtlasSourceIdentity(
                    "atlas.ts",
                    "makeAtlas",
                    left,
                ),
                bytes: png(),
            },
        ],
        left,
    );
    writeBundle(text);
    const identity = configuredSpriteAtlasBundle(left)!.identity;
    assert.equal(configuredSpriteAtlasBundle(right)!.identity, identity);
    for (const path of [
        "dist/src/lazy-helper.js",
        "node_modules/playwright-core/index.mjs",
    ]) {
        const file = resolve(right, path);
        writeFileSync(file, "// changed transitive producer");
        assert.throws(
            () => configuredSpriteAtlasBundle(right),
            /producer differs/,
        );
        writeFileSync(file, "// identical fixture input");
    }
});
