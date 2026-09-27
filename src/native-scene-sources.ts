import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

/** The implementation units the checked-in scene aggregate includes, in order. */
export function sceneAggregateSources(aggregate: string): string[] {
    const sources = [
        ...readFileSync(aggregate, "utf8").matchAll(
            /^#include "([^"\r\n]+\.cpp)"/gm,
        ),
    ].map((match) => resolve(dirname(aggregate), match[1]!));
    if (sources.length === 0)
        throw new Error(`No implementation units in ${aggregate}.`);
    return sources;
}
