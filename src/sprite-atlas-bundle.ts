/** An explicit device-produced input, never a fallback for a failed host bake. */
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { isAbsolute, relative, resolve } from "node:path";
import { PNG } from "pngjs";
import ts from "typescript";
import { repositoryModuleClosure } from "./bake-cache.js";
import { executedModuleScript } from "./executed-module-script.js";
import {
    isNonemptyString,
    isRecord,
    jsonValue,
    recordOf,
} from "./json-fields.js";
import { readUpstreamPin } from "./upstream-source.js";
import { contentDigest, hashEntries, listFiles } from "./tooling/records.js";
import {
    isRelativeSpecifier,
    moduleSpecifiers,
} from "./typescript-module-specifiers.js";

export const spriteAtlasBundleVariable = "BBLITE_SPRITE_ATLAS_BUNDLE";
const format = "android-sprite-atlas-v1";

interface AtlasSource {
    module: string;
    exportName: string;
    files: Record<string, string>;
    scriptSha256: string;
}

interface AtlasImage {
    sha256: string;
    width: number;
    height: number;
}

/** Observed, not inferred from the build host or a requested launch option. */
export interface AtlasDevice {
    serial: string;
    buildFingerprint: string;
    model: string;
    api: string;
    browserPackage: string;
    browserApks: Record<string, string>;
    browserVersion: Record<string, string>;
    commandLine: string;
    gpu: string;
    deviceScaleFactor: number;
}

interface AtlasEntry {
    source: AtlasSource;
    image: AtlasImage;
    png: string;
}

interface AtlasBundle {
    format: typeof format;
    pin: ReturnType<typeof readUpstreamPin>;
    producerSha256: string;
    device: AtlasDevice;
    entries: AtlasEntry[];
}

export interface SpriteAtlasProvenance {
    kind: typeof format;
    bundleSha256: string;
    pin: AtlasBundle["pin"];
    producerSha256: string;
    device: AtlasDevice;
    source: AtlasSource;
    image: AtlasImage;
}

function digest(bytes: string | Uint8Array): string {
    return createHash("sha256").update(bytes).digest("hex");
}

function boundedPath(root: string, path: string): string {
    const local = relative(root, resolve(root, path)).replaceAll("\\", "/");
    if (
        !local ||
        local === ".." ||
        local.startsWith("../") ||
        isAbsolute(local)
    )
        throw new Error(
            `Sprite atlas bundle source is outside its repository: ${path}`,
        );
    return local;
}

export function spriteAtlasSourceIdentity(
    modulePath: string,
    exportName: string,
    root = process.cwd(),
): AtlasSource {
    const module = boundedPath(root, modulePath);
    if (!/^[A-Za-z_$][\w$]*$/.test(exportName))
        throw new Error("Sprite atlas bundle requires a named factory export.");
    const closure = repositoryModuleClosure([module], root);
    if (!closure)
        throw new Error(`Cannot resolve atlas source closure: ${module}`);
    const files: Record<string, string> = {};
    for (const file of closure) {
        const path = boundedPath(root, file.path);
        const parsed = ts.createSourceFile(
            path,
            file.source.toString("utf8"),
            ts.ScriptTarget.ES2022,
            true,
        );
        if (
            moduleSpecifiers(parsed).some(
                (specifier) => !isRelativeSpecifier(specifier.text),
            )
        )
            throw new Error(
                `Device atlas bundles require repository-relative imports: ${path}`,
            );
        const visit = (node: ts.Node): void => {
            if (
                ts.isCallExpression(node) &&
                node.expression.kind === ts.SyntaxKind.ImportKeyword &&
                (node.arguments.length !== 1 ||
                    !ts.isStringLiteralLike(node.arguments[0]!))
            )
                throw new Error(
                    `Device atlas bundles require static import paths: ${path}`,
                );
            ts.forEachChild(node, visit);
        };
        visit(parsed);
        files[path] = digest(file.source);
    }
    return {
        module,
        exportName,
        files: Object.fromEntries(
            Object.entries(files).sort(([a], [b]) => a.localeCompare(b)),
        ),
        scriptSha256: digest(executedModuleScript(module, exportName)),
    };
}

/** The suite server admits both explicit and extensionless TypeScript imports. */
export function spriteAtlasRequestPaths(source: AtlasSource): string[] {
    return [
        "/scene.html",
        "/scene.js",
        "/favicon.ico",
        ...Object.keys(source.files).flatMap((path) => [
            `/${path}`,
            `/${path.replace(/\.ts$/, ".js")}`,
            `/${path.replace(/\.ts$/, "")}`,
        ]),
    ];
}

/** Include the serving/transpilation code, not just the command-line wrapper. */
export function spriteAtlasProducerIdentity(root = process.cwd()): string {
    const inputs = [
        "tools/bake-android-sprite-atlas.mjs",
        "dist/src",
        "package-lock.json",
        "node_modules/typescript",
        "node_modules/playwright-core",
        "node_modules/pngjs",
    ];
    return hashEntries(
        inputs.flatMap((input) => {
            const files = listFiles(resolve(root, input));
            if (!files.length)
                throw new Error(`Missing atlas producer input: ${input}`);
            return files.map(
                (file) => `${boundedPath(root, file)}\t${contentDigest(file)}`,
            );
        }),
    );
}

const isDigest = (value: unknown): value is string =>
    typeof value === "string" && /^[a-f0-9]{64}$/.test(value);
const positive = (value: unknown): value is number =>
    typeof value === "number" && Number.isFinite(value) && value > 0;
const stringRecord = (value: unknown): value is Record<string, string> =>
    recordOf(value, isNonemptyString) && Object.keys(value).length > 0;

function isDevice(value: unknown): value is AtlasDevice {
    return (
        isRecord(value) &&
        [
            value.serial,
            value.buildFingerprint,
            value.model,
            value.api,
            value.browserPackage,
            value.commandLine,
            value.gpu,
        ].every(isNonemptyString) &&
        recordOf(value.browserApks, isDigest) &&
        Object.keys(value.browserApks).length > 0 &&
        stringRecord(value.browserVersion) &&
        [
            value.browserVersion.product,
            value.browserVersion.revision,
            value.browserVersion.userAgent,
            value.browserVersion.jsVersion,
            value.browserVersion.protocolVersion,
        ].every(isNonemptyString) &&
        validGpu(value.gpu) &&
        positive(value.deviceScaleFactor)
    );
}

function validGpu(text: unknown): boolean {
    if (typeof text !== "string") return false;
    try {
        const gpu: unknown = JSON.parse(text);
        return (
            isRecord(gpu) &&
            Array.isArray(gpu.devices) &&
            gpu.devices.length > 0 &&
            gpu.devices.every(
                (device) =>
                    isRecord(device) &&
                    isNonemptyString(device.vendorString) &&
                    isNonemptyString(device.deviceString),
            ) &&
            isRecord(gpu.featureStatus) &&
            isNonemptyString(gpu.featureStatus["2d_canvas"])
        );
    } catch {
        return false;
    }
}

function isEntry(value: unknown): value is AtlasEntry {
    return (
        isRecord(value) &&
        isRecord(value.source) &&
        isNonemptyString(value.source.module) &&
        isNonemptyString(value.source.exportName) &&
        recordOf(value.source.files, isDigest) &&
        isDigest(value.source.scriptSha256) &&
        isRecord(value.image) &&
        isDigest(value.image.sha256) &&
        positive(value.image.width) &&
        Number.isInteger(value.image.width) &&
        positive(value.image.height) &&
        Number.isInteger(value.image.height) &&
        isNonemptyString(value.png)
    );
}

function isBundle(value: unknown): value is AtlasBundle {
    return (
        isRecord(value) &&
        value.format === format &&
        isRecord(value.pin) &&
        [value.pin.package, value.pin.version, value.pin.sourceVersion].every(
            isNonemptyString,
        ) &&
        isDigest(value.producerSha256) &&
        isDevice(value.device) &&
        Array.isArray(value.entries) &&
        value.entries.length > 0 &&
        value.entries.every(isEntry)
    );
}

function imageBytes(entry: AtlasEntry): Buffer {
    const bytes = Buffer.from(entry.png, "base64");
    if (
        bytes.toString("base64") !== entry.png ||
        digest(bytes) !== entry.image.sha256
    )
        throw new Error(
            `Sprite atlas bundle PNG hash differs: ${entry.source.module}`,
        );
    const image = PNG.sync.read(bytes);
    if (
        image.width !== entry.image.width ||
        image.height !== entry.image.height
    )
        throw new Error(
            `Sprite atlas bundle PNG dimensions differ: ${entry.source.module}`,
        );
    return bytes;
}

/** Producer and consumer use the same validation, including every entry. */
function validate(bundle: AtlasBundle, root: string): void {
    if (JSON.stringify(bundle.pin) !== JSON.stringify(readUpstreamPin(root)))
        throw new Error("Sprite atlas bundle pin differs.");
    if (bundle.producerSha256 !== spriteAtlasProducerIdentity(root))
        throw new Error(
            "Sprite atlas bundle producer differs; produce a new bundle.",
        );
    const seen = new Set<string>();
    for (const entry of bundle.entries) {
        const key = `${entry.source.module}#${entry.source.exportName}`;
        if (seen.has(key))
            throw new Error(`Duplicate sprite atlas bundle entry: ${key}`);
        seen.add(key);
        const current = spriteAtlasSourceIdentity(
            entry.source.module,
            entry.source.exportName,
            root,
        );
        if (JSON.stringify(entry.source) !== JSON.stringify(current))
            throw new Error(`Sprite atlas bundle source differs: ${key}`);
        imageBytes(entry);
    }
}

export function createSpriteAtlasBundle(
    device: AtlasDevice,
    entries: readonly { source: AtlasSource; bytes: Uint8Array }[],
    root = process.cwd(),
): string {
    const bundle: AtlasBundle = {
        format,
        pin: readUpstreamPin(root),
        producerSha256: spriteAtlasProducerIdentity(root),
        device,
        entries: entries.map(({ source, bytes }) => {
            const image = PNG.sync.read(Buffer.from(bytes));
            return {
                source,
                image: {
                    sha256: digest(bytes),
                    width: image.width,
                    height: image.height,
                },
                png: Buffer.from(bytes).toString("base64"),
            };
        }),
    };
    if (!isBundle(bundle))
        throw new Error("Incomplete sprite atlas bundle provenance.");
    validate(bundle, root);
    return `${JSON.stringify(bundle, null, 2)}\n`;
}

/** No memo: changing either path or bytes in one process must invalidate generation. */
export function configuredSpriteAtlasBundle(root = process.cwd()):
    | {
          identity: string;
          bundle: AtlasBundle;
      }
    | undefined {
    const path = process.env[spriteAtlasBundleVariable];
    if (path === undefined) return undefined;
    if (!path.trim())
        throw new Error(`${spriteAtlasBundleVariable} must name a bundle.`);
    const bytes = readFileSync(resolve(root, path));
    const bundle = jsonValue(
        JSON.parse(bytes.toString("utf8")),
        isBundle,
        "Invalid sprite atlas bundle.",
    );
    validate(bundle, root);
    return { identity: digest(bytes), bundle };
}

export function bundledSpriteAtlas(
    modulePath: string,
    exportName: string,
):
    | {
          bytes: Uint8Array;
          provenance: SpriteAtlasProvenance;
      }
    | undefined {
    const configured = configuredSpriteAtlasBundle();
    if (!configured) return undefined;
    const module = boundedPath(process.cwd(), modulePath);
    const entry = configured.bundle.entries.find(
        (candidate) =>
            candidate.source.module === module &&
            candidate.source.exportName === exportName,
    );
    if (!entry)
        throw new Error(
            `Sprite atlas bundle has no entry for ${module}#${exportName}.`,
        );
    return {
        bytes: imageBytes(entry),
        provenance: {
            kind: format,
            bundleSha256: configured.identity,
            pin: configured.bundle.pin,
            producerSha256: configured.bundle.producerSha256,
            device: configured.bundle.device,
            source: entry.source,
            image: entry.image,
        },
    };
}
