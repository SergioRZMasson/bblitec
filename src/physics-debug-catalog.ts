import { float32Literal, stringLiteral } from "./cpp-literals.js";
import type {
    PhysicsDebugGeometry,
    PhysicsDebugShape,
} from "./physics-debug-geometry.js";

/** Transport order is the native HP constructor call order, with no body state. */
interface PhysicsDebugDescriptor {
    type: string;
    parameters: number[];
    indices: number[];
    children: PhysicsDebugDescriptor[];
}
interface PhysicsDebugCatalogEntry {
    descriptor: PhysicsDebugDescriptor;
    shapeIdentity: string;
    geometry: PhysicsDebugGeometry;
}

export function physicsDebugDescriptor(value: unknown): PhysicsDebugDescriptor {
    if (!value || typeof value !== "object" || Array.isArray(value))
        throw new Error(
            "Physics constructor inputs require a descriptor object.",
        );
    const record = value as Record<string, unknown>;
    if (
        Object.keys(record).length !== 4 ||
        !["type", "parameters", "indices", "children"].every((key) =>
            Object.hasOwn(record, key),
        )
    ) {
        throw new Error(
            "Physics constructor inputs contain unrepresented descriptor fields.",
        );
    }
    if (
        typeof record.type !== "string" ||
        !Array.isArray(record.parameters) ||
        !record.parameters.every(
            (lane) =>
                typeof lane === "number" && Number.isFinite(Math.fround(lane)),
        ) ||
        !Array.isArray(record.indices) ||
        !record.indices.every(
            (lane: unknown): lane is number =>
                typeof lane === "number" &&
                Number.isInteger(lane) &&
                lane >= 0 &&
                lane <= 0xffffffff,
        ) ||
        !Array.isArray(record.children)
    ) {
        throw new Error(
            "Physics constructor inputs require finite float32 parameters and uint32 indices.",
        );
    }
    const result = {
        type: record.type,
        parameters: record.parameters.map(Math.fround),
        indices: [...record.indices],
        children: record.children.map(physicsDebugDescriptor),
    };
    // Validate semantic arity before admitting this record to a generated catalog.
    physicsDebugShapeFromDescriptor(result);
    return result;
}

function physicsDebugShapeFromDescriptor(
    descriptor: PhysicsDebugDescriptor,
): PhysicsDebugShape {
    const { type, parameters: p, indices, children } = descriptor;
    const count = (length: number) => {
        if (
            p.length !== length ||
            (type !== "MESH" && indices.length !== 0) ||
            (type !== "CONTAINER" && children.length !== 0)
        ) {
            throw new Error(
                `Physics constructor inputs: ${type} descriptor arity changed.`,
            );
        }
    };
    const v = (offset: number): [number, number, number] => [
        p[offset]!,
        p[offset + 1]!,
        p[offset + 2]!,
    ];
    const q = (offset: number): [number, number, number, number] => [
        ...v(offset),
        p[offset + 3]!,
    ];
    switch (type) {
        case "SPHERE":
            count(4);
            return { type, center: v(0), radius: p[3]! };
        case "BOX":
            count(10);
            return { type, center: v(0), rotation: q(3), extents: v(7) };
        case "CAPSULE":
        case "CYLINDER":
            count(7);
            return { type, pointA: v(0), pointB: v(3), radius: p[6]! };
        case "CONVEX_HULL":
            count(p.length);
            return { type, positions: p };
        case "MESH":
            count(p.length);
            return { type, positions: p, indices };
        case "HEIGHTFIELD":
            count(5 + p[0]! * p[1]!);
            return {
                type,
                samplesX: p[0]!,
                samplesZ: p[1]!,
                scale: v(2),
                heights: p.slice(5),
            };
        case "CONTAINER":
            count(children.length * 10);
            return {
                type,
                children: children.map((child, index) => ({
                    shape: physicsDebugShapeFromDescriptor(child),
                    position: v(index * 10),
                    rotation: q(index * 10 + 3),
                    scale: v(index * 10 + 7),
                })),
            };
        default:
            throw new Error(
                `Physics constructor inputs: unrepresented shape '${type}'.`,
            );
    }
}

export async function materializePhysicsDebugCatalog(
    inputs: unknown,
): Promise<PhysicsDebugCatalogEntry[]> {
    const { materializePhysicsDebugGeometry, physicsDebugShapeIdentity } =
        await import("./physics-debug-geometry.js");
    if (!Array.isArray(inputs))
        throw new Error(
            "Physics constructor input inventory must be an array.",
        );
    const entries = new Map<string, PhysicsDebugCatalogEntry>();
    for (const input of inputs) {
        const descriptor = physicsDebugDescriptor(input);
        const shape = physicsDebugShapeFromDescriptor(descriptor);
        const shapeIdentity = physicsDebugShapeIdentity(shape);
        if (!entries.has(shapeIdentity))
            entries.set(shapeIdentity, {
                descriptor,
                shapeIdentity,
                geometry: await materializePhysicsDebugGeometry(shape),
            });
    }
    return [...entries.values()].sort((left, right) =>
        left.shapeIdentity.localeCompare(right.shapeIdentity),
    );
}

export const physicsDebugCatalogPath =
    "upstream/src/physics_debug_geometry.cpp";
export function renderPhysicsDebugCatalog(
    entries: readonly PhysicsDebugCatalogEntry[],
): string {
    // Every constructor input is constant data compared in place: nothing is
    // allocated or initialized at run time, and a mesh collider's hundreds
    // of thousands of lanes never pass through a stack temporary.
    const constants: string[] = [];
    const shapeView = (input: PhysicsDebugDescriptor): string => {
        const children = input.children.map(shapeView);
        const name = `shape_${constants.length / 3}`;
        constants.push(
            `constexpr std::array<float, ${input.parameters.length}> ${name}_parameters{${input.parameters.map(float32Literal).join(",")}};`,
            `constexpr std::array<std::uint32_t, ${input.indices.length}> ${name}_indices{${input.indices.join(",")}};`,
            `constexpr std::array<ShapeView, ${children.length}> ${name}_children{${children.join(",")}};`,
        );
        return `ShapeView{${stringLiteral(input.type)}, ${name}_parameters, ${name}_indices, ${name}_children.data(), ${name}_children.size()}`;
    };
    const catalog = entries.map(
        (entry, index) =>
            `CatalogEntry{${shapeView(entry.descriptor)}, positions_${index}, indices_${index}}`,
    );
    return `// Generated opaque HP_Shape debug geometry. Constructor identity is validated in full.
#include <bblite/pal_physics_debug.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string_view>
namespace bbl::pal {
namespace {
struct ShapeView {
    std::string_view type;
    std::span<const float> parameters;
    std::span<const std::uint32_t> indices;
    const ShapeView* children{};
    std::size_t child_count{};
};
struct CatalogEntry {
    ShapeView shape;
    std::span<const float> positions;
    std::span<const std::uint32_t> indices;
};
bool matches(const PhysicsDebugShapeDescriptor& descriptor, const ShapeView& shape) {
    return descriptor.type == shape.type && std::ranges::equal(descriptor.parameters, shape.parameters) &&
           std::ranges::equal(descriptor.indices, shape.indices) &&
           std::ranges::equal(descriptor.children, std::span{shape.children, shape.child_count}, matches);
}
${entries
    .map(
        (
            entry,
            index,
        ) => `constexpr std::array<float, ${entry.geometry.positions.length}> positions_${index}{${entry.geometry.positions.map(float32Literal).join(",")}};
constexpr std::array<std::uint32_t, ${entry.geometry.indices.length}> indices_${index}{${entry.geometry.indices.join(",")}};`,
    )
    .join("\n")}
${constants.join("\n")}
constexpr std::array<CatalogEntry, ${catalog.length}> catalog{${catalog.join(",\n")}};
} // namespace
PhysicsDebugGeometry materialized_physics_debug_geometry(const PhysicsDebugShapeDescriptor& descriptor) {
    if (collect_physics_debug_descriptor(descriptor)) return {};
    for (const auto& entry : catalog)
        if (matches(descriptor, entry.shape)) return {entry.positions, entry.indices};
    throw std::runtime_error("Physics debug geometry has no materialized match for the complete " + descriptor.type + " constructor inputs.");
}
} // namespace bbl::pal
`;
}
