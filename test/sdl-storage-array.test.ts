import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import test from "node:test";
import { discoverDevelopmentTools } from "../src/development-tools.js";
import { developmentVcpkgInstall } from "../src/vcpkg-install.js";

const metal = process.platform === "darwin";

test(
    "SDL storage arrays preserve views, mip hazards and cycled allocations",
    {
        skip: !(metal
            ? process.env.BBLITE_TEST_METAL === "1"
            : process.platform === "linux" &&
              process.env.BBLITE_TEST_VULKAN === "1"),
    },
    () => {
        const tools = discoverDevelopmentTools();
        assert.ok(
            tools.cmake && tools.cxx && tools.bbliteTint,
            "CMake, C++ and pinned bblite-tint are required.",
        );
        const output = resolve("artifacts/test-sdl-storage-array");
        mkdirSync(output, { recursive: true });
        const shaders = {
            write2d: `@group(0) @binding(0) var image:texture_storage_2d<rgba32float,write>;
@group(0) @binding(1) var<uniform> value:vec4f;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) p:vec3u) {
textureStore(image,vec2i(p.xy),value); }`,
            writeArray: `@group(0) @binding(0) var image:texture_storage_2d_array<rgba32float,write>;
@group(0) @binding(1) var<uniform> value:vec4f;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) p:vec3u) {
textureStore(image,vec2i(p.xy),i32(p.z),value+vec4f(f32(p.z),0,0,0)); }`,
            addArray: `@group(0) @binding(0) var image:texture_storage_2d_array<rgba32float,read_write>;
@group(0) @binding(1) var<uniform> value:vec4f;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) p:vec3u) {
textureStore(image,vec2i(p.xy),i32(p.z),textureLoad(image,vec2i(p.xy),i32(p.z))+value); }`,
            // SDL's readonly storage slot is a sampled image without a sampler.
            readArray: `@group(0) @binding(0) var image:texture_2d_array<f32>;
@group(0) @binding(1) var<storage,read_write> result:array<vec4f>;
@group(0) @binding(2) var<storage,read> input:array<vec4f>;
@group(0) @binding(3) var<uniform> offset:vec4f;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) p:vec3u) {
result[p.x]=textureLoad(image,vec2i(0),i32(p.x),0)+input[p.x]+offset; }`,
            sampleCube: `@group(0) @binding(0) var image:texture_cube<f32>;
@group(0) @binding(1) var sourceSampler:sampler;
@group(0) @binding(2) var<storage,read_write> result:array<vec4f>;
@group(0) @binding(3) var<storage,read> input:array<vec4f>;
@group(0) @binding(4) var<uniform> offset:vec4f;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) p:vec3u) {
let directions=array<vec3f,6>(vec3f(1,0,0),vec3f(-1,0,0),vec3f(0,1,0),vec3f(0,-1,0),vec3f(0,0,1),vec3f(0,0,-1));
result[p.x]=textureSampleLevel(image,sourceSampler,directions[p.x],0)+input[p.x]+offset; }`,
        };
        const run = (command: string, args: string[]): string =>
            execFileSync(command, args, {
                encoding: "utf8",
                windowsHide: true,
                maxBuffer: 16 * 1024 * 1024,
            });
        for (const [name, source] of Object.entries(shaders)) {
            const input = join(output, `${name}.wgsl`);
            writeFileSync(input, source);
            run(tools.bbliteTint, [
                input,
                "--entry-point",
                "main",
                "--stage",
                "compute",
                metal ? "--msl" : "--spirv",
                join(output, `${name}.${metal ? "msl" : "spv"}`),
                "--layout-json",
                join(output, `${name}.json`),
            ]);
        }
        const dependencies = developmentVcpkgInstall();
        const prefix = join(
            dependencies.installedDirectory,
            dependencies.triplet,
        );
        const sdlDirectory =
            process.env.BBLITE_TEST_SDL_DIR ?? join(prefix, "share/sdl3");
        assert.ok(
            existsSync(join(sdlDirectory, "SDL3Config.cmake")),
            `SDL3 package is missing from the selected SDK: ${sdlDirectory}`,
        );
        writeFileSync(
            join(output, "configure.log"),
            run(tools.cmake, [
                "-S",
                "test/fixtures/sdl-storage-array",
                "-B",
                join(output, "build"),
                "-G",
                "Ninja",
                `-DCMAKE_CXX_COMPILER=${tools.cxx}`,
                "-DCMAKE_BUILD_TYPE=Release",
                `-DCMAKE_PREFIX_PATH=${prefix}`,
                // An earlier SDK's SDL3_DIR otherwise survives in CMakeCache.txt.
                `-DSDL3_DIR=${sdlDirectory}`,
            ]),
        );
        writeFileSync(
            join(output, "build.log"),
            run(tools.cmake, [
                "--build",
                join(output, "build"),
                "--parallel",
                "2",
            ]),
        );
        const result = spawnSync(
            join(output, "build/sdl_storage_array_check"),
            [output],
            {
                encoding: "utf8",
                windowsHide: true,
                timeout: 60000,
                maxBuffer: 16 * 1024 * 1024,
                env: {
                    ...process.env,
                    VK_KHRONOS_VALIDATION_VALIDATE_SYNC: "1",
                    ...(metal ? { MTL_DEBUG_LAYER: "1" } : {}),
                    SDL_ASSERT: "always_ignore",
                },
            },
        );
        const log = result.stdout + result.stderr;
        writeFileSync(join(output, "run.log"), log);
        assert.equal(result.status, 0, result.error?.message ?? log);
        if (metal) assert.match(log, /Metal API Validation Enabled/);
        // The Vulkan loader can report validation failures directly to stderr.
        assert.doesNotMatch(
            log,
            /VUID-|Validation Error|SYNC-HAZARD|Validation layers not found|failed assertion|MTLDebug.*error/i,
        );
        assert.match(
            result.stdout,
            /sdl-storage-array-check: 8 cases passed with (Vulkan|Metal) validation/,
        );
    },
);
