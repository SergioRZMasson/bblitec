import { pageBase64Script } from "./browser-harness.js";

/** Shared by host bakes and device bundle production. */
export function executedModuleScript(
    relativePath: string,
    exportName: string,
): string {
    const specifier = `/${relativePath.replace(/\.ts$/, ".js")}`;
    return `${pageBase64Script}
window.__runModuleExport = () =>
            import(${JSON.stringify(specifier)}).then((module) => {
                    const factory = module[${JSON.stringify(exportName)}];
                    if (typeof factory !== "function") {
                        throw new Error(
                            "Module export ${exportName} is not a function."
                        );
                    }
                    const value = factory();
                    if (!ArrayBuffer.isView(value)) return value;
                    return bblBase64(new Uint8Array(
                        value.buffer, value.byteOffset, value.byteLength));
                });
`;
}
