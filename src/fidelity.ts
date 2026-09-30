type FidelityRisk = "low" | "medium" | "high";

export interface CompileAdaptation {
    id: string;
    category:
        | "asset-materialization"
        | "async"
        | "browser-erasure"
        | "determinism"
        | "language"
        | "platform"
        | "rendering";
    sourceSemantics: string;
    nativeSemantics: string;
    risk: FidelityRisk;
    validation: string[];
}

interface ShaderInvariant {
    id: string;
    upstreamModule: string;
    upstreamMarker: string;
    nativeBehavior: string;
    validation: string[];
}

export interface RendererFidelityManifest {
    sourceLanguage: "WGSL";
    emittedSources: Array<"HLSL" | "MSL">;
    compiledArtifacts: Array<"DXIL" | "SPIR-V">;
    bindingContract: {
        vertexUniformSpace: number;
        sampledTextureSpace: number;
        fragmentUniformSpace: number;
    };
    textureContract: {
        /** Every material binding samples its image through the texture's own format. */
        materialTextures: "texture-format";
        environment: "linear-rgba16f";
        brdfLut: "linear-rgba16f";
    };
    invariants: ShaderInvariant[];
}
