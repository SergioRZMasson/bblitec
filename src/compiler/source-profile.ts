import type { NativeDeclaration } from "./native-declarations.js";

/**
 * The declarations that time the rest of a native body under the function's
 * profile name (`bblite/source_profile.hpp`). The index is registered once,
 * by the body's own static.
 */
export function sourceProfileScope(
    name: string,
    names: { function: string; scope: string },
    cppString: (text: string) => string,
): NativeDeclaration[] {
    return [
        {
            kind: "declaration",
            attributes: "static ",
            type: "const std::size_t",
            name: names.function,
            initializer: `bbl::profile::register_source_function(${cppString(name)})`,
        },
        {
            kind: "declaration",
            type: "const bbl::profile::SourceScope",
            name: names.scope,
            initializer: names.function,
            initialization: "direct",
        },
    ];
}
