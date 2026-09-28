import { splitUiCssList, uiCssValueTokens } from "./ui-css-syntax.js";

const length = /^(?:0|(?:\d+(?:\.\d*)?|\.\d+)(?:px|rem|em|vw|vh|%))$/i;
const gradient = /^(?:repeating-)?(?:linear|radial|conic)-gradient\(/i;

function position(
    tokens: readonly string[],
): readonly [string, string] | undefined {
    const horizontal: Readonly<Record<string, string>> = {
        left: "0%",
        center: "50%",
        right: "100%",
    };
    const vertical: Readonly<Record<string, string>> = {
        top: "0%",
        center: "50%",
        bottom: "100%",
    };
    if (tokens.length === 0) return ["0%", "0%"];
    if (tokens.length > 2) return undefined;
    let [x = "center", y = "center"] = tokens.map((token) =>
        token.toLowerCase(),
    );
    if (x === "top" || x === "bottom" || y === "left" || y === "right")
        [x, y] = [y, x];
    const axis = (value: string, keywords: Readonly<Record<string, string>>) =>
        Object.hasOwn(keywords, value)
            ? keywords[value]
            : length.test(value)
              ? value
              : undefined;
    const px = axis(x, horizontal),
        py = axis(y, vertical);
    return px !== undefined && py !== undefined ? [px, py] : undefined;
}

/** Project gradient layers into RmlUi decorators, retaining each independent image box. */
export function uiGradientBackground(value: string): string | undefined {
    const layers: string[] = [];
    for (const source of splitUiCssList(value)) {
        const values = uiCssValueTokens(source, "/");
        const image = values?.shift();
        if (!values || !image || !gradient.test(image) || !image.endsWith(")"))
            return undefined;
        const tokens = values.map((token) => token.toLowerCase());
        if (!tokens.length) {
            layers.push(image);
            continue;
        }
        // Gradients without a specified size fill their positioning area. Sized
        // repetition needs tiling and is deliberately outside this projection.
        const repeats = tokens.filter((token) => token === "no-repeat");
        if (repeats.length !== 1) return undefined;
        tokens.splice(tokens.indexOf("no-repeat"), 1);
        const slash = tokens.indexOf("/");
        const at = position(slash < 0 ? tokens : tokens.slice(0, slash));
        const size = slash < 0 ? ["100%", "100%"] : tokens.slice(slash + 1);
        if (size.length === 1) size.push("auto");
        if (
            !at ||
            size.length !== 2 ||
            size.some((token) => token !== "auto" && !length.test(token))
        )
            return undefined;
        layers.push(
            `${image} padding-box / ${size.map((token) => (token === "auto" ? "100%" : token)).join(" ")} / ${at.join(" ")}`,
        );
    }
    return layers.length ? layers.join(",") : undefined;
}
