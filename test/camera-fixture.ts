import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { CameraLowerer } from "../src/lowering/camera-lowerer.js";
import { LoweringContext } from "../src/lowering/context.js";
import { pinnedMatrixHeader } from "../src/lowering/pinned-matrix.js";

/** Lower the production camera units and their headers for a native fixture. */
export function cameraSources(
    directory: string,
    options: { arcRotate?: boolean; free?: boolean } = {},
): string[] {
    const context = new LoweringContext();
    const lowerer = new CameraLowerer(context);
    const headers = join(directory, "include/bblite/upstream");
    mkdirSync(headers, { recursive: true });
    const sources: string[] = [];
    const write = (name: string, text: string): void => {
        const path = join(directory, `${name}.cpp`);
        writeFileSync(path, text);
        sources.push(path);
    };
    const controls = lowerer.lowerControls();
    writeFileSync(join(headers, "camera_controls.hpp"), controls.header);
    write("controls", controls.source);
    if (options.arcRotate) {
        const camera = lowerer.lowerArcRotateFactory();
        writeFileSync(join(headers, "camera_math.hpp"), camera.header);
        writeFileSync(
            join(headers, "pinned_matrix.hpp"),
            pinnedMatrixHeader(context),
        );
        write("arc", camera.source);
    }
    if (options.free) write("free", lowerer.lowerFreeFactory().source);
    return sources;
}
