import type ts from "typescript";
import type { DataType } from "./data-types.js";
import type { LoweringServices } from "./lowering-services.js";
import { renderNativeDeclaration } from "./native-declarations.js";

/** One runtime input of an emitted body, by its C++ name. */
export interface RecentStringsParameter {
    readonly name: string;
    readonly type: DataType | undefined;
    readonly byReference: boolean;
}

/**
 * A pure function of numbers that builds a string -- a Map key spelled from
 * coordinates -- answers repeated arguments from its recent results
 * (`bbl::js::RecentStrings`): the result is a function of the arguments
 * alone, and a string is a value, so reuse is unobservable. Every emitter of
 * a function body asks here through `functionBodyPrologue`, whatever the
 * function's shape. `parameters` are
 * every input of the emitted body, by C++ name; an emitter whose body has any
 * other (a field channel, a closure capture) does not ask. Returns the body
 * wrapped in the memo with the memo's local, or undefined for a body that
 * does not qualify.
 */
function rememberRecentStrings(
    context: Pick<
        LoweringServices,
        "allocateTemporaryCppName" | "evaluationOrder"
    >,
    declaration: ts.FunctionLikeDeclaration,
    returnType: DataType | undefined,
    parameters: readonly RecentStringsParameter[],
    lines: readonly string[],
): { lines: string[]; memo: string } | undefined {
    if (
        returnType?.kind !== "string" ||
        parameters.length === 0 ||
        !parameters.every(
            (parameter) =>
                parameter.type?.kind === "number" && !parameter.byReference,
        ) ||
        !context.evaluationOrder.isPure(declaration)
    )
        return undefined;
    const memo = context.allocateTemporaryCppName("recent_results");
    return {
        memo,
        lines: [
            `static thread_local bbl::js::RecentStrings<${parameters.length}> ${memo};`,
            `return ${memo}.remember({${parameters.map((parameter) => parameter.name).join(", ")}}, [&]() -> std::string {`,
            ...lines.map((line) => `    ${line}`),
            "});",
        ],
    };
}

/**
 * A function body's prologue, in the one order every body emitter uses: the
 * `--source-profile` scope, then the recent-results memo, so a profiled
 * function's scope counts its cache hits too. `parameters` is undefined for a
 * body with inputs beside them, which never remembers. Returns the body's
 * lines and the locals the prologue adds.
 */
export function functionBodyPrologue(
    context: Pick<
        LoweringServices,
        | "allocateTemporaryCppName"
        | "evaluationOrder"
        | "sourceProfileScopeDeclarations"
    >,
    declaration: ts.FunctionLikeDeclaration,
    returnType: DataType | undefined,
    parameters: readonly RecentStringsParameter[] | undefined,
    lines: readonly string[],
): { lines: string[]; locals: string[] } {
    const remembered = parameters
        ? rememberRecentStrings(
              context,
              declaration,
              returnType,
              parameters,
              lines,
          )
        : undefined;
    const profiled = context.sourceProfileScopeDeclarations(declaration);
    return {
        lines: [
            ...profiled.map(renderNativeDeclaration),
            ...(remembered?.lines ?? lines),
        ],
        locals: [
            ...profiled.map((local) => local.name),
            ...(remembered ? [remembered.memo] : []),
        ],
    };
}
