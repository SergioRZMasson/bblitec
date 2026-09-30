/**
 * `left ?? right` evaluates its right operand only when the left is null or
 * undefined. Whatever the right side emits to prepare its value -- a call
 * result pinned for two reads, a search's index and bounds test -- belongs
 * to that arm, so a left that is present runs none of it.
 */
import assert from "node:assert/strict";
import test from "node:test";
import { compileSource } from "../src/compiler.js";
import {
    optionalNativeFixtureTools,
    runGeneratedProgram,
} from "./native-fixture.js";

// The fallback indexes with a draw from a stored closure: the draw is pinned
// once for its two reads (the element's `x` and `y`), and a present left must
// leave the generator's sequence untouched.
const drawingFallback = `
    interface Tile { x: number; y: number }
    function makeCounter(seed: number): () => number {
        let a = seed;
        return () => {
            a += 1;
            return a / 10;
        };
    }
    const draw = makeCounter(0);
    const cities: Tile[] = [{ x: 1, y: 2 }];
    if (Math.random() > 2) cities.pop();
    const land: Tile[] = [{ x: 3, y: 4 }, { x: 5, y: 6 }];
    const start = cities[0] ?? land[Math.floor(draw() * land.length)]!;
    if (start.x !== 1 || start.y !== 2) throw new Error("left operand");
    if (draw() !== 0.1) throw new Error("the fallback drew while the left was present");
`;

test("a fallback's pinned call stays inside the arm the select takes", () => {
    const { cpp } = compileSource(drawingFallback, {
        fileName: "nullish-fallback.ts",
    });
    assert.match(
        cpp,
        /v_start = \(static_cast<bool>\(v_bblite_nullish_\d+\) \? v_bblite_nullish_\d+ : \(\[&\]\(\) -> bblscene::Tile \{\n\[\[maybe_unused\]\] const double v_bblite_shared_result_\d+ = /,
    );
});

test("a fallback that can itself miss selects its handle and flag once", () => {
    const { cpp } = compileSource(
        `
        import {
            addToScene, createBox, createEngine, createSceneContext,
            createSphere, registerScene, startEngine,
        } from "@babylonjs/lite";
        async function main() {
            const engine = await createEngine({});
            const scene = createSceneContext(engine);
            const sphere = createSphere(engine, { diameter: 1 });
            sphere.name = "hero";
            addToScene(scene, sphere);
            addToScene(scene, createBox(engine, { size: 1 }));
            const picked =
                scene.meshes.find((m) => m.name === "villain") ?? scene.meshes[0];
            if (picked) picked.position.y = 2;
            await registerScene(scene);
            await startEngine(engine);
        }
        void main();
        `,
        { fileName: "nullish-search.ts" },
    );
    // The fallback's bounds test runs only after the search missed, and the
    // handle and its found flag are both read from the one selection.
    assert.match(
        cpp,
        /v_bblite_nullish_selection_\d+ = \(\[&\]\(\) -> std::pair<[^\n]*\{\nif \(v_bblite_scene_mesh_found_\d+\) return \{v_bblite_scene_mesh_match_\d+, true\};\nconst std::size_t v_bblite_scene_mesh_index_\d+ = /,
    );
    assert.match(cpp, /v_picked = v_bblite_nullish_selection_\d+\.first;/);
    assert.match(
        cpp,
        /v_bblite_element_found_\d+ = v_bblite_nullish_selection_\d+\.second;/,
    );
});

const tools = optionalNativeFixtureTools(false);
test(
    "a present left runs none of the fallback",
    { skip: !tools },
    () => {
        const { cpp } = compileSource(drawingFallback, {
            fileName: "nullish-fallback.ts",
        });
        runGeneratedProgram(tools!, "nullish-fallback-arm", cpp);
    },
);
