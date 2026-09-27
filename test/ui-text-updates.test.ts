import assert from "node:assert/strict";
import test from "node:test";
import { compileSource } from "../src/compiler.js";
import { runRmlUiFixture } from "./native-fixture.js";

test("plain text updates retain nodes and hidden HUDs render named-font glyphs", (t) => {
    runRmlUiFixture(t, "ui-text-updates");
});

test("retained CSS preserves ordered font families for native resolution", () => {
    const result = compileSource(`
        import { createEngine } from "@babylonjs/lite";
        await createEngine({});
        const hud = document.createElement("div");
        hud.style.cssText = "font-family:'Missing, Font', Verdana, sans-serif;font-weight:bold;";
        hud.textContent = "throw 1 / 3";
        document.body.appendChild(hud);
    `);
    assert.match(
        result.cpp,
        /font-family:'Missing, Font', Verdana, sans-serif/,
    );
});
