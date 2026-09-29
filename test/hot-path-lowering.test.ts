import assert from "node:assert/strict";
import test from "node:test";
import { compileSource } from "../src/compiler.js";

// The lowering a hot scene loop leans on: a chunk store keyed by a
// coordinate string, read through class methods many times a frame.
const source = `
    let prefix = "p";
    const labels = new Map<number, string>();
    function key(x: number, y: number): string {
        return x + "," + y;
    }
    function prefixed(x: number): string {
        return prefix + x;
    }
    function label(id: number): string {
        let text = labels.get(id);
        if (text === undefined) {
            text = "label " + id;
            labels.set(id, text);
        }
        return text;
    }
    class World {
        private readonly cells = new Map<string, number>();
        read(x: number, y: number): number {
            return this.cells.get(key(x, y)) ?? 0;
        }
        write(x: number, y: number, value: number): void {
            this.cells.set(key(x, y), value);
        }
    }
    function total(world: World): number {
        let sum = 0;
        for (let i = 0; i < 4; i++) sum += world.read(i, i + 1);
        return sum;
    }
    const world = new World();
    world.write(1, 2, 3);
    prefix = "q";
    console.log(total(world), prefixed(2), label(3), label(3));
`;

test("a pure number-to-string function remembers its recent results", () => {
    const { cpp } = compileSource(source);
    assert.match(
        cpp,
        /std::string key\(double (v_fn\d+_x), double (v_fn\d+_y)\) \{\s+static thread_local bbl::js::RecentStrings<2> (\w+);\s+return \3\.remember\(\{\1, \2\}, \[&\]\(\) -> std::string \{/,
    );
    // Reading a module binding the program reassigns is not pure, and the
    // label cache reads a map: `key` is the one remembering function.
    assert.equal(cpp.match(/\.remember\(/g)?.length, 1);
});

test("only a function that asks nothing of the host remembers its results", () => {
    const { cpp } = compileSource(`
        function cell(x: number, y: number): string {
            return Math.floor(x) + "," + String(y);
        }
        function stamped(x: number): string {
            return x + "@" + Date.now();
        }
        function timed(x: number): string {
            return x + ":" + performance.now();
        }
        function drawn(x: number): string {
            return x + ":" + Math.random();
        }
        function stored(x: number): string {
            return localStorage.getItem("k" + x) ?? "";
        }
        function outer(x: number): string {
            return cell(x, x) + stamped(x);
        }
        class Clock {
            get now(): number {
                return Date.now();
            }
        }
        function clocked(x: number): string {
            return x + "@" + new Clock().now;
        }
        console.log(cell(1, 2), timed(3), drawn(4), stored(5), outer(6), clocked(7));
    `);
    const remembering = [
        ...cpp.matchAll(
            /std::string (\w+)\([^)]*\) \{\s+static thread_local bbl::js::RecentStrings/g,
        ),
    ].map((match) => match[1]);
    assert.deepEqual(remembering, ["cell"]);
});

/** The body each `RecentStrings` memo computes a miss with. */
function rememberedBodies(cpp: string): string[] {
    return [
        ...cpp.matchAll(
            /\.remember\(\{[^}]*\}, \[&\]\(\) -> std::string \{([\s\S]*?)\n\s*\}\);/g,
        ),
    ].map((match) => match[1]!);
}

test("every function shape of a pure number-to-string body remembers its results", () => {
    const { cpp } = compileSource(`
        const labels = new Map<string, number>();
        function declared(x: number, z: number): string {
            return x + "," + z;
        }
        const arrowKey = (x: number, z: number): string => x + ":" + z;
        const expressionKey = function (x: number, z: number): string {
            return x + ";" + z;
        };
        class Keys {
            static of(x: number, z: number): string {
                return x + "/" + z;
            }
            at(x: number, z: number): string {
                return x + "|" + z;
            }
        }
        const keys = new Keys();
        for (let i = 0; i < 3; i++) {
            labels.set(declared(i, i), 1);
            labels.set(arrowKey(i, i), 2);
            labels.set(expressionKey(i, i), 3);
            labels.set(Keys.of(i, i), 4);
            labels.set(keys.at(i, i), 5);
        }
        console.log(labels.size);
    `);
    const bodies = rememberedBodies(cpp);
    assert.equal(bodies.length, 5, cpp);
    for (const separator of [",", ":", ";", "/", "|"])
        assert.ok(
            bodies.some((body) => body.includes(`, "${separator}", `)),
            `the body spelling '${separator}' remembers`,
        );
    // The arrow const and the static method are shared bodies: each memo is
    // keyed by its body's own runtime parameters.
    for (const separator of [":", "/"])
        assert.match(
            cpp,
            new RegExp(
                String.raw`static thread_local bbl::js::RecentStrings<2> (\w+);\s+return \1\.remember\(\{(fn\d+_recursive_arg_0), (fn\d+_recursive_arg_1)\}, \[&\]\(\) -> std::string \{\s+\[\[maybe_unused\]\] double \w+ = \2;\s+\[\[maybe_unused\]\] double \w+ = \3;\s+return bbl::js::concat\(bbl::js::NumberPart\(\w+\), "${separator}"`,
            ),
            separator,
        );
});

test("an impure body of any function shape does not remember its results", () => {
    const { cpp } = compileSource(`
        let offset = 0;
        const labels = new Map<string, number>();
        function declared(x: number): string {
            return x + "@" + Date.now();
        }
        const arrowKey = (x: number): string => x + ":" + performance.now();
        const expressionKey = function (x: number): string {
            return x + ";" + Math.random();
        };
        class Keys {
            private readonly prefix: string;
            constructor(prefix: string) {
                this.prefix = prefix;
            }
            static of(x: number): string {
                return x + "/" + offset;
            }
            at(x: number): string {
                return this.prefix + x;
            }
        }
        const keys = new Keys("k");
        for (let i = 0; i < 3; i++) {
            offset += i;
            labels.set(declared(i), 1);
            labels.set(arrowKey(i), 2);
            labels.set(expressionKey(i), 3);
            labels.set(Keys.of(i), 4);
            labels.set(keys.at(i), 5);
        }
        console.log(labels.size);
    `);
    assert.doesNotMatch(cpp, /RecentStrings/);
});

test("a closure over its caller's runtime binding does not remember its results", () => {
    // Each call of `run` builds `key` over another `prefix`: a memo keyed by
    // `x` alone would answer one closure with another's result.
    const { cpp } = compileSource(`
        const labels = new Map<string, number>();
        function run(prefix: number): void {
            const key = (x: number): string => prefix + ":" + x;
            for (let i = 0; i < 3; i++) labels.set(key(i), i);
        }
        run(1);
        run(2);
        console.log(labels.size);
    `);
    assert.doesNotMatch(cpp, /RecentStrings/);
});

test("a function reaching the host through mutual recursion does not remember its results", () => {
    // Whichever function is decided first reaches the other while it is still
    // undecided; the other's answer must not be kept on that assumption.
    for (const calls of ["early(2), late(3)", "late(3), early(2)"]) {
        const { cpp } = compileSource(`
            function early(n: number): string {
                return n > 0 ? late(n - 1) : String(Date.now());
            }
            function late(n: number): string {
                return early(n);
            }
            console.log(${calls});
        `);
        assert.doesNotMatch(cpp, /RecentStrings/, calls);
    }
});

test("a shared method body borrows its environment for the one call", () => {
    const { cpp } = compileSource(source);
    assert.match(
        cpp,
        /struct (bbl_environment_\w+) \{\s+std::remove_reference_t<std::shared_ptr<bbl::js::Map<std::string, double>>>& capture0;/,
    );
    assert.match(
        cpp,
        /make_closure\(bblscene::bbl_environment_\w+\{v_bblite_world_fields_cells_\d+\}, bblscene::bbl_recursive_fn\d+_group\)/,
    );
    assert.doesNotMatch(cpp, /\.capture0\.get\(\)/);
});

test("a local function that stores itself owns the caller's bindings", () => {
    // The closure `tick` stores itself, so it outlives the call that built
    // it: its environment must own the caller's bindings, not borrow them.
    const { cpp } = compileSource(`
        const pending: Array<() => void> = [];
        function start(limit: number): void {
            let count = 0;
            function tick(): void {
                count++;
                if (count < limit) pending.push(tick);
            }
            tick();
        }
        start(3);
        while (pending.length > 0) pending.pop()!();
    `);
    assert.doesNotMatch(cpp, /std::remove_reference_t<[^>]+>& capture\d+;/);
});

test("an owned optional local moves into the return", () => {
    const { cpp } = compileSource(source);
    assert.match(
        cpp,
        /bbl::js::Nullable<std::string> (v_fn\d+_text) = [^;]+;[\s\S]*return std::move\(\(\*\1\)\);/,
    );
});

test("--source-profile scopes the selected source functions only", () => {
    const plain = compileSource(source);
    assert.doesNotMatch(
        plain.cpp,
        /register_source_function|source_profile\.hpp/,
    );
    assert.ok(!plain.manifest.features.includes("profile:source"));

    const profiled = compileSource(source, {
        sourceProfile: ["World.read", "total"],
    });
    assert.match(profiled.cpp, /#include <bblite\/source_profile\.hpp>/);
    assert.match(
        profiled.cpp,
        /static const std::size_t (\w+) = bbl::profile::register_source_function\("World\.read"\);\s+const bbl::profile::SourceScope \w+\{\1\};/,
    );
    assert.match(
        profiled.cpp,
        /register_source_function\("total"\);\s+const bbl::profile::SourceScope/,
    );
    assert.doesNotMatch(
        profiled.cpp,
        /register_source_function\("World\.write"\)/,
    );
    assert.doesNotMatch(profiled.cpp, /register_source_function\("key"\)/);
    assert.ok(profiled.manifest.features.includes("profile:source"));
});

test("--source-profile refuses a name that times no function", () => {
    // A misspelt member and an async function would otherwise leave their
    // profile silently empty; the matched name alone does not refuse.
    assert.throws(
        () =>
            compileSource(source, {
                sourceProfile: ["total", "World.reads", "World.read"],
            }),
        /^Error: --source-profile selects no timed source function named 'World\.reads'\./,
    );
    assert.throws(
        () =>
            compileSource(
                `
                import { createEngine } from "@babylonjs/lite";
                async function main() {
                    const engine = await createEngine({});
                }
            `,
                { sourceProfile: ["main"] },
            ),
        /selects no timed source function named 'main'\. .*async functions/,
    );
});
