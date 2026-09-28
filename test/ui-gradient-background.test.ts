import assert from "node:assert/strict";
import { mkdirSync, writeFileSync } from "node:fs";
import test from "node:test";
import { compileSource } from "../src/compiler.js";
import { uiGradientBackground } from "../src/ui-gradient-background.js";
import { runRmlUiFixture } from "./native-fixture.js";

test("gradient background layers retain independent dimensions, positions and nested color functions", () => {
    assert.equal(
        uiGradientBackground("linear-gradient(red,blue)"),
        "linear-gradient(red,blue)",
    );
    assert.equal(
        uiGradientBackground(
            "linear-gradient(rgb(1,2,3),rgba(4,5,6,.5)) right bottom/12px 25% no-repeat,radial-gradient(red,blue) top left/auto 10px no-repeat",
        ),
        "linear-gradient(rgb(1,2,3),rgba(4,5,6,.5)) padding-box / 12px 25% / 100% 100%,radial-gradient(red,blue) padding-box / 100% 10px / 0% 0%",
    );
    for (const value of [
        "linear-gradient(red,blue) center/2px 22px repeat",
        "linear-gradient(red,blue) center/2px 22px",
        "linear-gradient(red,blue) center/-2px 22px no-repeat",
        "linear-gradient(red,blue) left 2px bottom 3px/2px 22px no-repeat",
        "linear-gradient(red,blue) center/contain no-repeat",
        "linear-gradient(red,blue) constructor/2px 22px no-repeat",
    ]) {
        assert.equal(uiGradientBackground(value), undefined, value);
        assert.throws(
            () =>
                compileSource(
                    `import {createEngine} from "@babylonjs/lite"; await createEngine({}); const e=document.createElement("div"); e.style.cssText=${JSON.stringify(`background:${value}`)}; document.body.appendChild(e);`,
                ),
            /gradient layers require/,
        );
    }
});

test("native sized layers preserve gradient coordinates, paint order, content and live layout", (t) => {
    const layers = uiGradientBackground(
        "linear-gradient(red,red) center/4px 32px no-repeat,linear-gradient(blue,blue) right bottom/40px 6px no-repeat",
    )!;
    const resized = uiGradientBackground(
        "linear-gradient(to right,red,blue) center/50% 25% no-repeat",
    )!;
    mkdirSync("artifacts/ui-gradient-background", { recursive: true });
    writeFileSync(
        "artifacts/ui-gradient-background/styles.hpp",
        `constexpr const char* layers = ${JSON.stringify(`decorator:${layers};`)};\nconstexpr const char* resized = ${JSON.stringify(`decorator:${resized};`)};\n`,
    );
    runRmlUiFixture(t, "ui-gradient-background");
});
